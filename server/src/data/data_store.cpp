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

    buildIndexes();

    // 车站-线路邻居索引：优先从本地缓存加载
    if (!tryLoadStationLineIndex()) {
        buildStationLineIndex();
        saveStationLineIndex();
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
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return nullptr;
    return &stations_[it->second];
}

const Train* DataStore::getTrain(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = train_index_.find(id);
    if (it == train_index_.end()) return nullptr;
    return &trains_[it->second];
}

Train* DataStore::getTrainMutable(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = train_index_.find(id);
    if (it == train_index_.end()) return nullptr;
    return &trains_[it->second];
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

    for (size_t i = 0; i < stations_.size(); ++i) {
        station_index_[stations_[i].id] = i;
    }
    for (size_t i = 0; i < trains_.size(); ++i) {
        train_index_[trains_[i].id] = i;
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
                // stop_type
                int stype = s.stop_type;
                if (stype == 0) {
                    if (i == 0) stype = 0;
                    else if (i == train.stops.size() - 1) stype = 3;
                    else stype = (s.arrival > 0 && s.departure > 0 && s.arrival == s.departure) ? 2 : 1;
                }
                jstops[i]["stop_type"] = stype;
                jstops[i]["stop_type_name"] = (stype == 0 ? "始发" : stype == 1 ? "停靠" : stype == 2 ? "通过" : "终到");
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
    saveStations();
    return s;
}

bool DataStore::updateStation(uint32_t id, const Station& updated) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return false;
    stations_[it->second] = updated;
    stations_[it->second].id = id;  // 保持 ID 不变
    saveStations();
    return true;
}

bool DataStore::removeStation(uint32_t id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return false;
    size_t idx = it->second;
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
    saveLines();
    return l;
}

bool DataStore::updateLine(uint32_t id, const Line& updated) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    for (auto& ln : lines_) {
        if (ln.id == id) {
            ln = updated;
            ln.id = id;
            saveLines();
            return true;
        }
    }
    return false;
}

bool DataStore::removeLine(uint32_t id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(lines_.begin(), lines_.end(),
        [id](const Line& l) { return l.id == id; });
    if (it == lines_.end()) return false;
    lines_.erase(it);
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

// ── 车站-线路-邻居索引 ──

// 从 data/station_line_index.json 加载索引缓存，失败返回 false
bool DataStore::tryLoadStationLineIndex() {
    std::string path = config::STATION_LINE_INDEX_FILE;
    if (!fs::exists(path)) return false;

    try {
        std::ifstream in(path);
        json j;
        in >> j;

        station_line_index_.clear();
        for (auto& [key, neighbors] : j.items()) {
            uint32_t sid = static_cast<uint32_t>(std::stoul(key));
            station_line_index_[sid] = neighbors.get<std::vector<LineNeighbor>>();
        }
        Logger::instance().info("Station-line index loaded from cache: "
            + std::to_string(station_line_index_.size()) + " stations");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().warn(std::string("Failed to load station-line index: ") + e.what());
        return false;
    }
}

// 将索引序列化为 JSON 并写入 data/station_line_index.json
void DataStore::saveStationLineIndex() const {
    std::string path = config::STATION_LINE_INDEX_FILE;
    try {
        json j;
        for (const auto& [sid, neighbors] : station_line_index_) {
            j[std::to_string(sid)] = neighbors;
        }
        std::ofstream out(path);
        out << j.dump();
        Logger::instance().info("Station-line index saved: "
            + std::to_string(station_line_index_.size()) + " stations");
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save station-line index: ") + e.what());
    }
}

// 从 lines_ 构建索引：遍历每条线路的站点序列，取相邻站对，Haversine 算距离，合并到 map
void DataStore::buildStationLineIndex() {
    station_line_index_.clear();

    for (const auto& line : lines_) {
        // 城市名 → 站点 ID
        std::vector<uint32_t> ids;
        for (const auto& city : line.stations) {
            bool found = false;
            for (const auto& st : stations_)
                if (st.city == city) {
                    ids.push_back(st.id);
                    found = true;
                    break;
                }
            if (!found)
                Logger::instance().warn("Line '" + line.name
                    + "': city '" + city + "' not found in stations");
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
