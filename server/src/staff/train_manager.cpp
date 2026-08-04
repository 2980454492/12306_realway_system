// train_manager.cpp — TrainManager 实现
#include "staff/train_manager.h"
#include "data/data_store.h"
#include "config.h"
#include "utils.h"
#include "system/logger.h"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
// 区间 key：出发站|到达站|线路ID（方向敏感 + 线路隔离）
std::string makeOccKey(uint32_t from, uint32_t to, uint32_t line_id) {
    return std::to_string(from) + "|" + std::to_string(to) + "|" + std::to_string(line_id);
}
}  // namespace

// ── 单例 ──

TrainManager& TrainManager::instance() {
    static TrainManager mgr;
    return mgr;
}

// ── 持久化 ──

bool TrainManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    loadOccupancy();
    return true;
}

std::string TrainManager::occKey(uint32_t from, uint32_t to, uint32_t line_id) const {
    return makeOccKey(from, to, line_id);
}

void TrainManager::loadOccupancy() {
    // 从所有 ACTIVE 列车重建区间占用表
    auto& ds = DataStore::instance();
    for (const auto& train : ds.getAllTrains()) {
        if (train.status != TrainStatus::ACTIVE) continue;
        addToOccupancyUnsafe(train);
    }
    Logger::instance().info("Occupancy table rebuilt from " +
        std::to_string(ds.getAllTrains().size()) + " trains");
}

void TrainManager::saveOccupancy() const {
    // 占用表由列车数据派生，无需单独持久化
}

// ── 占用表操作 ──

void TrainManager::addToOccupancy(const Train& train) {
    std::lock_guard<std::mutex> lock(mutex_);
    addToOccupancyUnsafe(train);
}

void TrainManager::addToOccupancyUnsafe(const Train& train) {
    auto segs = buildSegments(train, DataStore::instance());
    for (const auto& seg : segs) {
        if (seg.enter_time <= 0 || seg.leave_time <= 0 || seg.line_id == 0) continue;
        auto key = occKey(seg.from_station, seg.to_station, seg.line_id);
        occupancy_[key].insert({seg.enter_time, seg.leave_time});
        occ_detail_[key].push_back({train.id, {seg.enter_time, seg.leave_time}});
    }
}

void TrainManager::removeFromOccupancy(const Train& train) {
    auto segs = buildSegments(train, DataStore::instance());
    for (const auto& seg : segs) {
        if (seg.enter_time <= 0 || seg.leave_time <= 0 || seg.line_id == 0) continue;
        auto key = occKey(seg.from_station, seg.to_station, seg.line_id);
        auto& set_ref = occupancy_[key];
        set_ref.erase({seg.enter_time, seg.leave_time});
        auto& vec = occ_detail_[key];
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [&](const auto& p) { return p.first == train.id; }), vec.end());
    }
}

// ── 校验 ──

TrainManager::ValidationResult TrainManager::validate(const Train& train, bool is_new) const {
    ValidationResult result;
    auto& ds = DataStore::instance();

    // 1. 车次号校验：新增须唯一
    auto* existing = ds.getTrain(train.id);
    if (!is_new && !existing) {
        result.error = "列车 " + train.id + " 不存在";
        return result;
    }
    if (is_new && existing && existing->status != TrainStatus::ARCHIVED) {
        result.error = "车次号 " + train.id + " 已存在";
        return result;
    }
    
    // 2. 日期校验：新增 ≥ MIN_NEW_TRAIN_DAYS 天，修改 ≥ MAX_ADVANCE_DAYS+1 天
    if (!train.valid_from.empty()) {
        if (!isFuture(train.valid_from, 365)) {
            result.error = "生效日期不能是过去";
            return result;
        }
        int min_days = is_new ? MIN_NEW_TRAIN_DAYS : (MAX_ADVANCE_DAYS + 1);
        auto tm = nowTm();
        tm.tm_mday += min_days;
        std::mktime(&tm);
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        if (train.valid_from < std::string(buf)) {
            result.error = std::string(is_new ? "新增" : "修改")
                + "列车须至少 " + std::to_string(min_days) + " 天后生效";
            return result;
        }
    } else if (is_new) {
        result.error = "请选择生效日期";
        return result;
    }

    // 3. 办客站至少 2（始发+终到）
    if (train.stops.size() < 2) {
        result.error = "至少需要始发站和终点站";
        return result;
    }

    // 4. 所有 stops 中的站在系统注册站内（O(n) 遍历 + O(1) 查）
    for (const auto& stop : train.stops) {
        if (!ds.getStation(stop.station_id)) {
            result.error = "站 ID " + std::to_string(stop.station_id) + " 不存在";
            return result;
        }
    }

    // 5. 同站时间合法性：到站 ≤ 发车（始发无到达、终到无发车、通过站到=发合法）
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
        if (s.arrival > 0 && s.departure > 0 && s.arrival > s.departure) {
            result.error = "第 " + std::to_string(i + 1) + " 站到站时间须早于发车时间";
            return result;
        }
    }

    // 6. 相邻停站：前站发车 < 后站到达（O(n) 遍历）
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

    auto& ds = DataStore::instance();
    auto segs = buildSegments(train, ds);
    for (const auto& seg : segs) {
        int new_dep = seg.enter_time;  // 从 from_station 发车/通过时间
        int new_arr = seg.leave_time;  // 到达 to_station 的时间
        if (new_dep <= 0 || new_arr <= 0 || seg.line_id == 0) continue;

        auto key = occKey(seg.from_station, seg.to_station, seg.line_id);
        auto it = occupancy_.find(key);
        if (it == occupancy_.end()) continue;

        for (const auto& [ex_dep, ex_arr] : it->second) {
            // 查找已占用车次号，排除自身
            std::string ex_tid;
            auto dit = occ_detail_.find(key);
            if (dit != occ_detail_.end()) {
                for (const auto& [tid, times] : dit->second) {
                    if (times.first == ex_dep && times.second == ex_arr) {
                        if (tid != train.id) ex_tid = tid;
                        break;
                    }
                }
            }
            if (ex_tid.empty()) continue;

            // ── 规则 1：发车间隔 ≥ 5 分钟 ──
            if (std::abs(new_dep - ex_dep) < SAFETY_MARGIN_MINUTES) {
                conflicts.push_back({ex_tid, seg.from_station, seg.to_station,
                    seg.line_id, ex_dep, ex_arr});
                continue;
            }

            // ── 规则 2：到达间隔 ≥ 5 分钟 ──
            if (std::abs(new_arr - ex_arr) < SAFETY_MARGIN_MINUTES) {
                conflicts.push_back({ex_tid, seg.from_station, seg.to_station,
                    seg.line_id, ex_dep, ex_arr});
                continue;
            }

            // ── 规则 3：禁止越行 ──
            // 后发车的必须先到站；但前车在到达站停靠时可让行
            if (new_dep > ex_dep && new_arr <= ex_arr) {
                // 前车是否在此站停靠？是则允许越行（前车在站台让出轨道）
                const Train* ex_train = ds.getTrain(ex_tid);
                bool ex_stops = false;
                if (ex_train) {
                    for (const auto& stop : ex_train->stops)
                        if (stop.station_id == seg.to_station && stop.stop_type == StopType::STOP){ 
                            ex_stops = true; 
                            break; 
                        }
                }
                if (!ex_stops) {
                    conflicts.push_back({ex_tid, seg.from_station, seg.to_station,
                        seg.line_id, ex_dep, ex_arr});
                    continue;
                }
            }
        }
    }
    return conflicts;
}

// ── 提交/审批共用校验 ──

TrainManager::CheckResult TrainManager::checkTrain(const Train& train, bool is_new) const {
    CheckResult result;
    auto vr = validate(train, is_new);
    if (!vr.valid) {
        result.error = vr.error;
        return result;
    }
    result.conflicts = detectConflicts(train);
    if (!result.conflicts.empty()) {
        result.error = "运行图冲突：与 " + result.conflicts[0].train_id + " 在区间重叠";
        return result;
    }
    result.valid = true;
    return result;
}

// ── 变更操作 ──

bool TrainManager::addTrain(const Train& train) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();
    ds.addTrain(train);
    addToOccupancyUnsafe(train);
    Logger::instance().info("Train added: " + train.id);
    return true;
}

TrainManager::DeleteResult TrainManager::deleteTrain(const std::string& train_id) {
    DeleteResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();

    auto* train = ds.getTrain(train_id);
    if (!train) { 
        result.error = "列车不存在";
        return result; 
    }
    if (train->status != TrainStatus::ACTIVE) {
        result.error = "列车非运行中状态";
        return result;
    }

    // 未出发已售车票检查待后续版本实现（需 OrderService 暴露跨用户查询接口）

    removeFromOccupancy(*train);
    ds.removeTrain(train_id);
    result.success = true;
    Logger::instance().info("Train removed: " + train_id);
    return result;
}

bool TrainManager::adjustSchedule(const std::string& train_id, const std::vector<Stop>& new_stops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();
    auto* train = ds.getTrainMutable(train_id);
    if (!train) return false;

    removeFromOccupancy(*train);
    train->stops = new_stops;
    addToOccupancyUnsafe(*train);
    Logger::instance().info("Schedule adjusted: " + train_id);
    return true;
}

TrainManager::UpdateResult TrainManager::updateTrain(const std::string& train_id, const Train& updated) {
    UpdateResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ds = DataStore::instance();
    auto* train = ds.getTrainMutable(train_id);
    if (!train) {
        result.error = "列车 " + train_id + " 不存在";
        return result;
    }

    // 1. 移除旧占用（避免自己和自己冲突）
    auto old_stops = train->stops;
    removeFromOccupancy(*train);

    // 2. 临时应用新 stops 检测冲突
    train->stops = updated.stops;
    auto new_conflicts = detectConflicts(*train);
    if (!new_conflicts.empty()) {
        // 回滚：恢复旧 stops + 旧占用
        train->stops = old_stops;
        addToOccupancyUnsafe(*train);
        result.error = "运行图冲突：与 " + new_conflicts[0].train_id + " 在区间重叠";
        return result;
    }

    // 3. 通过 → 覆盖其他字段 + 加入新占用
    train->type = updated.type;
    train->seat_config = updated.seat_config;
    train->valid_from = updated.valid_from;
    train->valid_until = updated.valid_until;
    addToOccupancyUnsafe(*train);
    Logger::instance().info("Train updated: " + train_id);
    result.success = true;
    return result;
}

const std::vector<Train>& TrainManager::getAllTrains() const {
    return DataStore::instance().getAllTrains();
}
