// data_store.cpp — DataStore 实现
#include "data/data_store.h"
#include "data/train_generator.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

DataStore& DataStore::instance() {
    static DataStore store;
    return store;
}

bool DataStore::initialize(const std::string& config_dir) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ready_) {
        Logger::instance().warn("DataStore already initialized");
        return true;
    }

    Logger::instance().info("Initializing DataStore from: " + config_dir);

    // 加载顺序：站点 → 线路 → 列车（列车依赖线路和站点）
    if (!loadStations(config_dir)) return false;
    if (!loadLines(config_dir)) return false;
    if (!loadTrains(config_dir)) return false;

    buildIndexes();
    ready_ = true;

    Logger::instance().info("DataStore ready: "
        + std::to_string(stations_.size()) + " stations, "
        + std::to_string(lines_.size()) + " lines, "
        + std::to_string(trains_.size()) + " trains");

    return true;
}

// ── 查询接口 ──

const Station* DataStore::getStation(uint32_t id) const {
    auto it = station_index_.find(id);
    if (it == station_index_.end()) return nullptr;
    return &stations_[it->second];
}

const Station* DataStore::getStationByName(const std::string& name) const {
    auto it = station_name_to_id_.find(name);
    if (it == station_name_to_id_.end()) return nullptr;
    return getStation(it->second);
}

const Line* DataStore::getLine(uint32_t id) const {
    auto it = line_index_.find(id);
    if (it == line_index_.end()) return nullptr;
    return &lines_[it->second];
}

const Train* DataStore::getTrain(const std::string& id) const {
    auto it = train_index_.find(id);
    if (it == train_index_.end()) return nullptr;
    return &trains_[it->second];
}

std::vector<const Train*> DataStore::getTrainsByStation(uint32_t station_id) const {
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
    line_index_.clear();
    train_index_.clear();
    station_name_to_id_.clear();

    for (size_t i = 0; i < stations_.size(); ++i) {
        station_index_[stations_[i].id] = i;
        station_name_to_id_[stations_[i].name] = stations_[i].id;
    }
    for (size_t i = 0; i < lines_.size(); ++i) {
        line_index_[lines_[i].id] = i;
    }
    for (size_t i = 0; i < trains_.size(); ++i) {
        train_index_[trains_[i].id] = i;
    }
}

// ── 加载实现 ──

bool DataStore::loadStations(const std::string& config_dir) {
    std::string path = config_dir + "/stations.json";
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

bool DataStore::loadLines(const std::string& config_dir) {
    std::string path = config_dir + "/lines.json";
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

bool DataStore::loadTrains(const std::string& config_dir) {
    std::string path = config_dir + "/trains.json";

    // 若 trains.json 已存在则直接加载（幂等）
    if (fs::exists(path)) {
        std::ifstream file(path);
        if (!file.is_open()) {
            Logger::instance().error("Failed to open: " + path);
            return false;
        }
        try {
            json j;
            file >> j;
            trains_ = j.get<std::vector<Train>>();
            Logger::instance().info("Loaded " + std::to_string(trains_.size()) + " trains from file");
            return true;
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("Failed to parse trains.json: ") + e.what());
            return false;
        }
    }

    // 不存在则自动生成
    Logger::instance().info("trains.json not found, generating...");
    trains_ = TrainGenerator::generate(lines_, stations_);

    // 保存生成结果
    try {
        json j = trains_;
        std::ofstream out(path);
        out << j.dump(2);
        out.close();
        Logger::instance().info("Generated and saved " + std::to_string(trains_.size()) + " trains");
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save trains.json: ") + e.what());
        // 生成成功了但保存失败——数据在内存中仍然可用
    }

    return true;
}

// ── 运行时变更 ──

void DataStore::addTrain(const Train& train) {
    std::lock_guard<std::mutex> lock(mutex_);
    trains_.push_back(train);
    train_index_[train.id] = trains_.size() - 1;
}

bool DataStore::removeTrain(const std::string& train_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = train_index_.find(train_id);
    if (it == train_index_.end()) return false;
    trains_[it->second].status = TrainStatus::ARCHIVED;
    return true;
}

bool DataStore::saveTrains(const std::string& config_dir) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string path = config_dir + "/trains.json";
    try {
        using json = nlohmann::json;
        json j = trains_;
        std::ofstream out(path);
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save trains.json: ") + e.what());
        return false;
    }
}
