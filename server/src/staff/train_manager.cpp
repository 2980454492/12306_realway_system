// train_manager.cpp — TrainManager 实现
#include "staff/train_manager.h"
#include "data/data_store.h"
#include "core/utils.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
    // 区间 key：小 ID 在前
    std::string makeOccKey(uint32_t a, uint32_t b) {
        if (a <= b) return std::to_string(a) + "|" + std::to_string(b);
        return std::to_string(b) + "|" + std::to_string(a);
    }
}  // namespace

// ── 单例 ──

TrainManager& TrainManager::instance() {
    static TrainManager mgr;
    return mgr;
}

// ── 持久化 ──

bool TrainManager::initialize(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_dir_ = data_dir;
    loadOccupancy();
    return true;
}

std::string TrainManager::occKey(uint32_t a, uint32_t b) const {
    return makeOccKey(a, b);
}

void TrainManager::loadOccupancy() {
    // 从所有 ACTIVE 列车重建区间占用表
    auto& ds = DataStore::instance();
    for (const auto& train : ds.getAllTrains()) {
        if (train.status != TrainStatus::ACTIVE) continue;
        addToOccupancy(train);
    }
    Logger::instance().info("Occupancy table rebuilt from " +
        std::to_string(ds.getAllTrains().size()) + " trains");
}

void TrainManager::saveOccupancy() const {
    // 占用表由列车数据派生，无需单独持久化
}

// ── 占用表操作 ──

void TrainManager::addToOccupancy(const Train& train) {
    for (size_t i = 0; i + 1 < train.stops.size(); ++i) {
        int enter = train.stops[i].departure;
        int leave = train.stops[i + 1].arrival;
        if (enter <= 0 || leave <= 0) continue;
        auto key = occKey(train.stops[i].station_id, train.stops[i + 1].station_id);
        occupancy_[key].insert({enter, leave});
        occ_detail_[key].push_back({train.id, {enter, leave}});
    }
}

void TrainManager::removeFromOccupancy(const Train& train) {
    for (size_t i = 0; i + 1 < train.stops.size(); ++i) {
        int enter = train.stops[i].departure;
        int leave = train.stops[i + 1].arrival;
        if (enter <= 0 || leave <= 0) continue;
        auto key = occKey(train.stops[i].station_id, train.stops[i + 1].station_id);
        auto& set_ref = occupancy_[key];
        set_ref.erase({enter, leave});
        // 清理 detail
        auto& vec = occ_detail_[key];
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [&](const auto& p) { return p.first == train.id; }), vec.end());
    }
}

// ── 校验 ──

TrainManager::ValidationResult TrainManager::validate(const Train& train, bool is_new) const {
    ValidationResult result;
    auto& ds = DataStore::instance();

    // 1. 车次号唯一
    if (is_new && ds.getTrain(train.id)) {
        result.error = "车次号 " + train.id + " 已存在";
        return result;
    }

    // 2. 停站数至少 2
    if (train.stops.size() < 2) {
        result.error = "至少需要 2 个停站";
        return result;
    }

    // 3. 所有站在系统注册站内
    for (const auto& stop : train.stops) {
        if (!ds.getStation(stop.station_id)) {
            result.error = "站 ID " + std::to_string(stop.station_id) + " 不存在";
            return result;
        }
    }

    // 4. 时间合法性：到站 < 发车（始发站无到达、终到站无发车除外）
    for (size_t i = 0; i < train.stops.size(); ++i) {
        const auto& s = train.stops[i];
        bool is_first = (i == 0), is_last = (i == train.stops.size() - 1);
        if (!is_first && s.arrival <= 0) {
            result.error = "第 " + std::to_string(i + 1) + " 站缺少到站时间";
            return result;
        }
        if (!is_last && s.departure <= 0) {
            result.error = "第 " + std::to_string(i + 1) + " 站缺少发车时间";
            return result;
        }
        // 到站时间 < 发车时间（同一站）
        if (s.arrival > 0 && s.departure > 0 && s.arrival >= s.departure) {
            result.error = "第 " + std::to_string(i + 1) + " 站到站时间须早于发车时间";
            return result;
        }
    }

    // 5. 相邻停站：前站发车 < 后站到达
    for (size_t i = 0; i + 1 < train.stops.size(); ++i) {
        int dep = train.stops[i].departure;
        int arr = train.stops[i + 1].arrival;
        if (dep > 0 && arr > 0 && dep >= arr) {
            result.error = "第 " + std::to_string(i + 1) + " 站发车须早于第 "
                         + std::to_string(i + 2) + " 站到站";
            return result;
        }
    }

    result.valid = true;
    return result;
}

// ── 冲突检测 ──

std::vector<TrainManager::ConflictDetail> TrainManager::detectConflicts(const Train& train) const {
    std::vector<ConflictDetail> conflicts;
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t i = 0; i + 1 < train.stops.size(); ++i) {
        int new_enter = train.stops[i].departure;
        int new_leave = train.stops[i + 1].arrival;
        if (new_enter <= 0 || new_leave <= 0) continue;

        auto key = occKey(train.stops[i].station_id, train.stops[i + 1].station_id);
        auto it = occupancy_.find(key);
        if (it == occupancy_.end()) continue;

        // 遍历该区间已有占用，检查重叠（含 5 分钟裕量）
        for (const auto& [ex_enter, ex_leave] : it->second) {
            int overlap_start = std::max(new_enter, ex_enter);
            int overlap_end   = std::min(new_leave, ex_leave);
            if (overlap_start < overlap_end + SAFETY_MARGIN_MINUTES) {
                // 查找冲突车次
                std::string conflict_train;
                auto dit = occ_detail_.find(key);
                if (dit != occ_detail_.end()) {
                    for (const auto& [tid, times] : dit->second) {
                        if (times.first == ex_enter && times.second == ex_leave) {
                            conflict_train = tid; break;
                        }
                    }
                }
                conflicts.push_back({conflict_train,
                    train.stops[i].station_id, train.stops[i + 1].station_id,
                    ex_enter, ex_leave});
            }
        }
    }
    return conflicts;
}

// ── 变更操作 ──

bool TrainManager::addTrain(const Train& train) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();
    ds.addTrain(train);
    addToOccupancy(train);
    Logger::instance().info("Train added: " + train.id);
    return true;
}

TrainManager::DeleteResult TrainManager::deleteTrain(const std::string& train_id) {
    DeleteResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();

    auto* train = ds.getTrain(train_id);
    if (!train) { result.error = "列车不存在"; return result; }
    if (train->status != TrainStatus::ACTIVE) {
        result.error = "列车非运行中状态"; return result;
    }

    // TODO: 检查未出发已售车票（需要 OrderService 暴露跨用户查询接口）

    removeFromOccupancy(*train);
    ds.removeTrain(train_id);
    result.success = true;
    Logger::instance().info("Train removed: " + train_id);
    return result;
}

bool TrainManager::adjustSchedule(const std::string& train_id, const std::vector<Stop>& new_stops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();
    auto* train = const_cast<Train*>(ds.getTrain(train_id));
    if (!train) return false;

    removeFromOccupancy(*train);
    train->stops = new_stops;
    addToOccupancy(*train);
    Logger::instance().info("Schedule adjusted: " + train_id);
    return true;
}

const std::vector<Train>& TrainManager::getAllTrains() const {
    return DataStore::instance().getAllTrains();
}
