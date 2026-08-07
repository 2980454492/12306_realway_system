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
    for (Train& train : trains_) {
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

Station* DataStore::getStation(uint32_t id) {
    std::unordered_map<uint32_t, size_t>::const_iterator it = station_index_.find(id);
    if (it == station_index_.end()) return nullptr;
    return &stations_[it->second];
}

Train* DataStore::getTrain(const std::string& id) {
    std::unordered_map<std::string, size_t>::const_iterator it = train_index_.find(id);
    if (it == train_index_.end()) return nullptr;
    return &trains_[it->second];
}

Line* DataStore::getLine(uint32_t id) {
    std::unordered_map<uint32_t, size_t>::const_iterator it = line_index_.find(id);
    if (it == line_index_.end()) return nullptr;
    return &lines_[it->second];
}

std::vector<const Train*> DataStore::getTrainsByStation(uint32_t station_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<const Train*> result;
    for (const Train& train : trains_) {
        for (const Stop& stop : train.stops) {
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
    city_to_ids_.clear();
    name_to_id_.clear();
    station_name_set_.clear();
    for (size_t i = 0; i < stations_.size(); ++i) {
        station_index_[stations_[i].id] = i;
        station_name_set_.insert(stations_[i].name);
        station_name_set_.insert(stations_[i].city);
        city_to_ids_[stations_[i].city].push_back(stations_[i].id);
        name_to_id_[stations_[i].name] = stations_[i].id;
    }

    train_index_.clear();
    for (size_t i = 0; i < trains_.size(); ++i) {
        train_index_[trains_[i].id] = i;
    }

    line_index_.clear();
    for (size_t i = 0; i < lines_.size(); ++i) {
        line_index_[lines_[i].id] = i;
    }

    station_line_index_.clear();
    for (const Line& line : lines_) {
        // 站名 → 站点 ID（O(1) 查索引）
        std::vector<uint32_t> ids;
        for (const std::string& name : line.stations) {
            uint32_t sid = stationNameToId(name);
            if (sid) ids.push_back(sid);
            else Logger::instance().warn("Line '" + line.name
                + "': station '" + name + "' not found");
        }
        if (ids.size() != line.stations.size() || ids.size() < 2) continue;

        for (size_t i = 0; i < ids.size(); ++i) {
            uint32_t cur = ids[i];
            std::vector<LineNeighbor> neighbors;

            if (i > 0) {
                Station* prev_st = getStation(ids[i - 1]);
                Station* cur_st = getStation(cur);
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
                Station* next_st = getStation(ids[i + 1]);
                Station* cur_st = getStation(cur);
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
            std::vector<LineNeighbor>& existing = station_line_index_[cur];
            existing.insert(existing.end(), neighbors.begin(), neighbors.end());
        }
    }

    // ── 车站-列车索引 ──
    station_train_index_.clear();
    for (const Train& train : trains_) {
        if (train.status != TrainStatus::ACTIVE) continue;
        for (size_t i = 0; i < train.stops.size(); ++i)
            station_train_index_[train.stops[i].station_id].push_back(
                {train.id, static_cast<int>(i)});
    }
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
    // 已归档同名覆盖：先清理旧索引，再更新已有槽位
    std::unordered_map<std::string, size_t>::const_iterator it = train_index_.find(train.id);
    if (it != train_index_.end()) {
        // 清理该车次在 station_train_index_ 中的旧条目
        const Train& old_train = trains_[it->second];
        for (const Stop& stop : old_train.stops)
            station_train_index_[stop.station_id].erase(
                std::remove_if(
                    station_train_index_[stop.station_id].begin(),
                    station_train_index_[stop.station_id].end(),
                    [&](const TrainStopEntry& e) { return e.train_id == train.id; }),
                station_train_index_[stop.station_id].end());
        trains_[it->second] = train;
    } else {
        trains_.push_back(train);
        train_index_[train.id] = trains_.size() - 1;
    }
    // 为新 stops 重建索引条目
    for (size_t i = 0; i < train.stops.size(); ++i)
        station_train_index_[train.stops[i].station_id].push_back(
            {train.id, static_cast<int>(i)});
}

bool DataStore::removeTrain(const std::string& train_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::unordered_map<std::string, size_t>::const_iterator it = train_index_.find(train_id);
    if (it == train_index_.end()) return false;
    trains_[it->second].status = TrainStatus::ARCHIVED;
    return true;
}

bool DataStore::saveTrains(){
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::string path = config::TRAINS_FILE;
    try {
        // line_name — 构建 line_id→name 映射避免三重循环
        std::unordered_map<uint32_t, std::string> line_id_to_name;
        for (const Line& ln : lines_)
            line_id_to_name[ln.id] = ln.name;

        json arr = json::array();
        for (const Train& train : trains_) {
            json jt = train;
            json& jstops = jt["stops"];
            for (size_t i = 0; i < jstops.size(); i++) {
                Stop stop = train.stops[i];
                // station_name
                Station* station = getStation(stop.station_id);
                jstops[i]["station_name"] = station ? station->name : "?";
                // stop_type：未初始化时动态计算
                StopType stype = stop.stop_type;
                if (stype == StopType::ORIGIN) {
                    if (i == 0)
                        stype = StopType::ORIGIN;
                    else if (i == train.stops.size() - 1)
                        stype = StopType::TERMINAL;
                    else
                        stype = (stop.arrival > 0 && stop.departure > 0 && stop.arrival == stop.departure)
                            ? StopType::PASS : StopType::STOP;
                }
                jstops[i]["stop_type"] = stype;
                // line_name — O(1) 查表，替代原三重循环
                if (stop.line_id > 0) {
                    std::unordered_map<uint32_t, std::string>::const_iterator it = line_id_to_name.find(stop.line_id);
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
    for (const T& item : items)
        if (item.id > max_id) max_id = item.id;
    return max_id + 1;
}

bool DataStore::addStation(Station& station) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (station.id == 0)
        station.id = nextId(stations_);
    stations_.push_back(station);
    station_index_[station.id] = stations_.size() - 1;
    station_name_set_.insert(station.name);
    station_name_set_.insert(station.city);
    name_to_id_[station.name] = station.id;
    city_to_ids_[station.city].push_back(station.id);
    saveStations();
    return true;
}

bool DataStore::updateStation(uint32_t station_id, const Station& station) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::unordered_map<uint32_t, size_t>::const_iterator it = station_index_.find(station_id);
    if (it == station_index_.end()) return false;
    // 旧数据清理：站名/城市变更时更新索引
    Station& old = stations_[it->second];
    if (old.name != station.name) {
        name_to_id_.erase(old.name);
        station_name_set_.erase(old.name);
        name_to_id_[station.name] = station_id;
        station_name_set_.insert(station.name);
    }
    if (old.city != station.city) {
        station_name_set_.erase(old.city);
        station_name_set_.insert(station.city);
        // 从旧城市的 ID 列表中移除
        std::vector<uint32_t>& old_ids = city_to_ids_[old.city];
        old_ids.erase(std::remove(old_ids.begin(), old_ids.end(), station_id), old_ids.end());
        city_to_ids_[station.city].push_back(station_id);
    }
    old = station;
    old.id = station_id;  // 保持 ID 不变
    saveStations();
    return true;
}

bool DataStore::removeStation(uint32_t station_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::unordered_map<uint32_t, size_t>::const_iterator it = station_index_.find(station_id);
    if (it == station_index_.end()) return false;
    size_t idx = it->second;
    // 清理索引
    Station& removed = stations_[idx];
    name_to_id_.erase(removed.name);
    station_name_set_.erase(removed.name);
    station_name_set_.erase(removed.city);
    std::vector<uint32_t>& city_ids = city_to_ids_[removed.city];
    city_ids.erase(std::remove(city_ids.begin(), city_ids.end(), station_id), city_ids.end());
    // swap-remove
    size_t last = stations_.size() - 1;
    if (idx != last) {
        std::swap(stations_[idx], stations_[last]);
        station_index_[stations_[idx].id] = idx;
    }
    stations_.pop_back();
    station_index_.erase(station_id);
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

bool DataStore::addLine(Line& line) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (line.id == 0)
        line.id = nextId(lines_);
    lines_.push_back(line);
    line_index_[line.id] = lines_.size() - 1;
    saveLines();
    return true;
}

bool DataStore::updateLine(uint32_t line_id, const Line& updated) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::unordered_map<uint32_t, size_t>::const_iterator it = line_index_.find(line_id);
    if (it == line_index_.end()) return false;
    lines_[it->second] = updated;
    lines_[it->second].id = line_id;
    saveLines();
    return true;
}

bool DataStore::removeLine(uint32_t line_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::unordered_map<uint32_t, size_t>::const_iterator it = line_index_.find(line_id);
    if (it == line_index_.end()) return false;
    size_t idx = it->second;
    size_t last = lines_.size() - 1;
    if (idx != last) {
        std::swap(lines_[idx], lines_[last]);
        line_index_[lines_[idx].id] = idx;
    }
    lines_.pop_back();
    line_index_.erase(line_id);
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