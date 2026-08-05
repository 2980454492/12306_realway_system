// data_store.cpp — DataStore 实现
#include "data/data_store.h"
#include "config.h"
#include "utils.h"
#include "system/logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

DataStore& DataStore::instance() {
    static DataStore store;
    return store;
}

bool DataStore::initialize() {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    if (ready_) {
        Logger::instance().warn("DataStore already initialized");
        return true;
    }

    Logger::instance().info("Initializing DataStore");

    // 加载顺序：站点 → 线路 → 列车（列车依赖线路和站点）
    if (!loadStations()) return false;
    if (!loadLines()) return false;
    if (!loadTrains()) return false;

    buildIndexes();  // 所有索引（含 station_line_index_）统一在此构建

    // 清理已过期的列车（valid_until 已过 → ARCHIVED）
    std::string today = todayStr();
    int archived = 0;
    for (auto& train : trains_) {
        if (!train.valid_until.empty() && train.valid_until < today
            && train.status == TrainStatus::ACTIVE) {
            train.status = TrainStatus::ARCHIVED;
            archived++;
        }
    }
    if (archived > 0) {
        saveTrains();
        Logger::instance().info("Archived " + std::to_string(archived) + " expired trains");
    }

    ready_ = true;

    Logger::instance().info("DataStore ready: "
        + std::to_string(stations_.size()) + " stations, "
        + std::to_string(lines_.size()) + " lines, "
        + std::to_string(trains_.size()) + " trains");

    return true;
}

// ── 查询接口 ──

const Station* DataStore::getStation(uint32_t id) const {
    // 不加锁：被 saveTrains 等内部方法在已持锁上下文中调用，
    // 加 shared_lock 会同线程重复加锁触发死锁。HTTP 单线程事件循环提供外部同步。
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return nullptr;
    return &stations_[it->second];
}

const Train* DataStore::getTrain(const std::string& id) const {
    auto it = train_index_.find(id);
    if (it == train_index_.end()) return nullptr;
    return &trains_[it->second];
}

Train* DataStore::getTrainMutable(const std::string& id) {
    return const_cast<Train*>(getTrain(id));
}

const Line* DataStore::getLine(uint32_t id) const {
    auto it = line_index_.find(id);
    if (it == line_index_.end()) return nullptr;
    return &lines_[it->second];
}

std::vector<const Train*> DataStore::getTrainsByStation(uint32_t station_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<const Train*> result;
    for (const auto& train : trains_) {
        for (const auto& stop : train.stops) {
            if (stop.station_id == station_id) {
                result.push_back(&train);
                break;  // 该车次停靠此站，已找到
            }
        }
    }
    return result;
}

// ── 索引构建 ──

void DataStore::buildIndexes() {
    station_index_.clear();
    train_index_.clear();
    line_index_.clear();
    station_name_set_.clear();
    city_to_ids_.clear();
    name_to_id_.clear();
    station_line_index_.clear();
    station_line_index_.clear();

    for (size_t i = 0; i < stations_.size(); ++i) {
        station_index_[stations_[i].id] = i;
        station_name_set_.insert(stations_[i].name);
        station_name_set_.insert(stations_[i].city);
        city_to_ids_[stations_[i].city].push_back(stations_[i].id);
        name_to_id_[stations_[i].name] = stations_[i].id;
    }
    for (size_t i = 0; i < trains_.size(); ++i) {
        train_index_[trains_[i].id] = i;
    }
    for (size_t i = 0; i < lines_.size(); ++i) {
        line_index_[lines_[i].id] = i;
    }
    for (const auto& line : lines_) {
        // 站名 → 站点 ID（O(1) 查索引）
        std::vector<uint32_t> ids;
        for (const auto& name : line.stations) {
            uint32_t sid = stationToId(name);
            if (sid) ids.push_back(sid);
            else Logger::instance().warn("Line '" + line.name
                + "': station '" + name + "' not found");
        }
        if (ids.size() != line.stations.size() || ids.size() < 2) continue;

        for (size_t i = 0; i < ids.size(); ++i) {
            uint32_t cur = ids[i];
            std::vector<LineNeighbor> neighbors;

            if (i > 0) {
                auto* prev_st = getStation(ids[i - 1]);
                auto* cur_st = getStation(cur);
                if (prev_st && cur_st) {
                    neighbors.push_back({
                        line.id, line.name,
                        ids[i - 1], prev_st->name,
                        haversineDist(*cur_st, *prev_st),
                        line.max_speed_kmh
                    });
                }
            }

            // 后一站（若存在）
            if (i + 1 < ids.size()) {
                auto* next_st = getStation(ids[i + 1]);
                auto* cur_st = getStation(cur);
                if (next_st && cur_st) {
                    neighbors.push_back({
                        line.id, line.name,
                        ids[i + 1], next_st->name,
                        haversineDist(*cur_st, *next_st),
                        line.max_speed_kmh
                    });
                }
            }

            // 合并到已有条目（一个站可能属于多条线路）
            auto& existing = station_line_index_[cur];
            existing.insert(existing.end(), neighbors.begin(), neighbors.end());
        }
    }

    Logger::instance().info("Station-line index built: "
        + std::to_string(station_line_index_.size()) + " stations");
}

// ── 加载实现 ──

bool DataStore::loadStations() {
    std::string path = config::STATIONS_FILE;
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open: " + path);
        return false;
    }

    try {
        json j;
        file >> j;
        stations_ = j.get<std::vector<Station>>();
        Logger::instance().info("Loaded " + std::to_string(stations_.size()) + " stations");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to parse stations.json: ") + e.what());
        return false;
    }
}

bool DataStore::loadLines() {
    std::string path = config::LINES_FILE;
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open: " + path);
        return false;
    }

    try {
        json j;
        file >> j;
        lines_ = j.get<std::vector<Line>>();
        Logger::instance().info("Loaded " + std::to_string(lines_.size()) + " lines");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to parse lines.json: ") + e.what());
        return false;
    }
}

bool DataStore::loadTrains() {
    std::string path = config::TRAINS_FILE;

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open: " + path);
        return false;
    }
    try {
        json j;
        file >> j;
        trains_ = j.get<std::vector<Train>>();
        Logger::instance().info("Loaded " + std::to_string(trains_.size()) + " trains");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to parse trains.json: ") + e.what());
        return false;
    }
}

// ── 运行时变更 ──

void DataStore::addTrain(const Train& train) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    // 已归档同名覆盖：更新已有槽位，不新增（避免 trains_ 中出现重复条目）
    auto it = train_index_.find(train.id);
    if (it != train_index_.end()) {
        trains_[it->second] = train;
        return;
    }
    trains_.push_back(train);
    train_index_[train.id] = trains_.size() - 1;
}

bool DataStore::removeTrain(const std::string& train_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = train_index_.find(train_id);
    if (it == train_index_.end()) return false;
    trains_[it->second].status = TrainStatus::ARCHIVED;
    return true;
}

bool DataStore::saveTrains() const {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::string path = config::TRAINS_FILE;
    try {
        // line_name — 构建 line_id→name 映射避免三重循环
        std::unordered_map<uint32_t, std::string> line_id_to_name;
        for (const auto& ln : lines_)
            line_id_to_name[ln.id] = ln.name;

        json arr = json::array();
        for (const auto& train : trains_) {
            json jt = train;
            auto& jstops = jt["stops"];
            for (size_t i = 0; i < jstops.size(); i++) {
                auto& s = train.stops[i];
                // station_name
                auto* st = getStation(s.station_id);
                jstops[i]["station_name"] = st ? st->name : "?";
                // stop_type：未初始化时动态计算
                StopType stype = s.stop_type;
                if (stype == StopType::ORIGIN) {
                    if (i == 0)
                        stype = StopType::ORIGIN;
                    else if (i == train.stops.size() - 1)
                        stype = StopType::TERMINAL;
                    else
                        stype = (s.arrival > 0 && s.departure > 0 && s.arrival == s.departure)
                            ? StopType::PASS : StopType::STOP;
                }
                jstops[i]["stop_type"] = stype;
                // line_name — O(1) 查表，替代原三重循环
                if (s.line_id > 0) {
                    auto it = line_id_to_name.find(s.line_id);
                    jstops[i]["line_name"] = (it != line_id_to_name.end()) ? it->second : "";
                } else {
                    jstops[i]["line_name"] = "";
                }
            }
            arr.push_back(jt);
        }
        std::ofstream out(path);
        out << arr.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save trains.json: ") + e.what());
        return false;
    }
}

// ── 站点管理 ──

/** 获取下一个可用 ID（当前最大 ID + 1） */
template<typename T>
static uint32_t nextId(const std::vector<T>& items) {
    uint32_t max_id = 0;
    for (const auto& item : items)
        if (item.id > max_id) max_id = item.id;
    return max_id + 1;
}

Station DataStore::addStation(const Station& station) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    Station s = station;
    if (s.id == 0)
        s.id = nextId(stations_);
    stations_.push_back(s);
    station_index_[s.id] = stations_.size() - 1;
    station_name_set_.insert(s.name);
    station_name_set_.insert(s.city);
    name_to_id_[s.name] = s.id;
    city_to_ids_[s.city].push_back(s.id);
    saveStations();
    return s;
}

bool DataStore::updateStation(uint32_t id, const Station& updated) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return false;
    // 旧数据清理：站名/城市变更时更新索引
    auto& old = stations_[it->second];
    if (old.name != updated.name) {
        name_to_id_.erase(old.name);
        station_name_set_.erase(old.name);
        name_to_id_[updated.name] = id;
        station_name_set_.insert(updated.name);
    }
    if (old.city != updated.city) {
        station_name_set_.erase(old.city);
        station_name_set_.insert(updated.city);
        // 从旧城市的 ID 列表中移除
        auto& old_ids = city_to_ids_[old.city];
        old_ids.erase(std::remove(old_ids.begin(), old_ids.end(), id), old_ids.end());
        city_to_ids_[updated.city].push_back(id);
    }
    old = updated;
    old.id = id;  // 保持 ID 不变
    saveStations();
    return true;
}

bool DataStore::removeStation(uint32_t id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return false;
    size_t idx = it->second;
    // 清理索引
    auto& removed = stations_[idx];
    name_to_id_.erase(removed.name);
    station_name_set_.erase(removed.name);
    station_name_set_.erase(removed.city);
    auto& city_ids = city_to_ids_[removed.city];
    city_ids.erase(std::remove(city_ids.begin(), city_ids.end(), id), city_ids.end());
    // swap-remove
    size_t last = stations_.size() - 1;
    if (idx != last) {
        std::swap(stations_[idx], stations_[last]);
        station_index_[stations_[idx].id] = idx;
    }
    stations_.pop_back();
    station_index_.erase(id);
    saveStations();
    return true;
}

bool DataStore::saveStations() const {
    std::string path = config::STATIONS_FILE;
    try {
        json j = stations_;
        std::ofstream out(path);
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save stations: ") + e.what());
        return false;
    }
}

// ── 线路管理 ──

Line DataStore::addLine(const Line& line) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    Line l = line;
    if (l.id == 0)
        l.id = nextId(lines_);
    lines_.push_back(l);
    line_index_[l.id] = lines_.size() - 1;
    saveLines();
    return l;
}

bool DataStore::updateLine(uint32_t id, const Line& updated) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = line_index_.find(id);
    if (it == line_index_.end()) return false;
    lines_[it->second] = updated;
    lines_[it->second].id = id;
    saveLines();
    return true;
}

bool DataStore::removeLine(uint32_t id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = line_index_.find(id);
    if (it == line_index_.end()) return false;
    size_t idx = it->second;
    size_t last = lines_.size() - 1;
    if (idx != last) {
        std::swap(lines_[idx], lines_[last]);
        line_index_[lines_[idx].id] = idx;
    }
    lines_.pop_back();
    line_index_.erase(id);
    saveLines();
    return true;
}

bool DataStore::saveLines() const {
    std::string path = config::LINES_FILE;
    try {
        json j = lines_;
        std::ofstream out(path);
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save lines: ") + e.what());
        return false;
    }
}