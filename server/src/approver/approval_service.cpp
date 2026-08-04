// approval_service.cpp — ApprovalService 实现
#include "approver/approval_service.h"
#include "staff/train_manager.h"
#include "data/data_store.h"
#include "config.h"
#include "utils.h"
#include "system/logger.h"
#include "system/wal.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

using json = nlohmann::json;

// ── 单例 ──

ApprovalService& ApprovalService::instance() {
    static ApprovalService svc;
    return svc;
}

// ── 持久化 ──

bool ApprovalService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string path = config::APPROVALS_FILE;
    if (!fs::exists(path)) {
        Logger::instance().info("No existing approvals file, starting fresh");
        return true;
    }

    try {
        std::ifstream in(path);
        json j;
        in >> j;
        approvals_ = j.get<std::vector<ApprovalRequest>>();
        Logger::instance().info("Loaded " + std::to_string(approvals_.size()) + " approvals");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to load approvals: ") + e.what());
        return false;
    }
}

void ApprovalService::saveApprovals() const {
    std::string path = config::APPROVALS_FILE;
    try {
        json j = approvals_;
        std::ofstream out(path);
        out << j.dump(2);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save approvals: ") + e.what());
    }
}

// ── 提交 ──

std::string ApprovalService::submit(ApprovalType type, const std::string& submitter_id,
                                    const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    ApprovalRequest req;
    req.id = generateUuid();
    req.type = type;
    req.submitter_id = submitter_id;
    req.payload = payload;
    req.submitted_at = nowIso();

    approvals_.push_back(req);
    saveApprovals();
    Logger::instance().info("Approval submitted: " + req.id);
    return req.id;
}

// ── 内部工具 ──

void ApprovalService::archivePendingTrain(const ApprovalRequest& req) {
    if (req.type != ApprovalType::CREATE_TRAIN)
        return;
    try {
        json payload = json::parse(req.payload);
        std::string tid = payload.value("id", "");
        auto* train = DataStore::instance().getTrainMutable(tid);
        if (train && train->status == TrainStatus::PENDING) {
            train->status = TrainStatus::ARCHIVED;
            DataStore::instance().saveTrains();
        }
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("archivePendingTrain failed: ") + e.what());
    }
}

// ── 停站时间 ──

namespace {

/** 根据车次号前缀返回列车最高设计时速（km/h） */
int getTrainMaxSpeed(const std::string& train_id) {
    if (train_id.empty()) return 120;
    switch (train_id[0]) {
        case 'G': return 350;
        case 'D': return 300;
        case 'C': return 350;
        case 'Z': return 160;
        case 'T': return 140;
        case 'K': return 120;
        case 'S': return 999;
        default:  return 120;
    }
}

/** 用线路站点顺序精确定位新站的插入位置。
 *  在 train.stops 中找到新站前后相邻城市对应的连续停站对，返回插入索引。
 *  找不到时回退到线路上第一对连续站。 */
int findStopInsertIndex(const Train& train, uint32_t line_id,
                        const std::string& st_city, const DataStore& ds) {
    // 1. 从线路站点顺序中找新站的前后邻站城市
    std::string prev_city, next_city;
    for (const auto& ln : ds.getAllLines()) {
        if (ln.id != line_id) continue;
        for (size_t i = 0; i < ln.stations.size(); i++) {
            if (ln.stations[i] == st_city) {
                if (i > 0) prev_city = ln.stations[i - 1];
                if (i + 1 < ln.stations.size()) next_city = ln.stations[i + 1];
                break;
            }
        }
        break;
    }

    // 2. 在列车停站中匹配这对相邻城市（必须同线路）
    if (!prev_city.empty() && !next_city.empty()) {
        for (size_t i = 0; i + 1 < train.stops.size(); i++) {
            if (train.stops[i].line_id != line_id
                || train.stops[i + 1].line_id != line_id)
                continue;
            auto* s1 = ds.getStation(train.stops[i].station_id);
            auto* s2 = ds.getStation(train.stops[i + 1].station_id);
            if (s1 && s2 && s1->city == prev_city && s2->city == next_city)
                return static_cast<int>(i + 1);
        }
    }

    // 3. 回退：线路上第一对连续站（兼容旧数据）
    for (size_t i = 0; i + 1 < train.stops.size(); i++) {
        if (train.stops[i].line_id == line_id
            && train.stops[i + 1].line_id == line_id)
            return static_cast<int>(i + 1);
    }
    return -1;
}

}  // namespace

ApprovalService::UpdateStopTimeResult ApprovalService::updateStopTime(
    const std::string& approval_id, int arrival, int departure) {

    UpdateStopTimeResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 找到审批
    auto it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) {
            return a.id == approval_id && a.type == ApprovalType::STOP_INSERT;
        });
    if (it == approvals_.end()) {
        result.error = "审批不存在或类型不匹配";
        return result;
    }

    // 2. 解析载荷
    json payload = json::parse(it->payload);
    std::string tid = payload.value("train_id", "");
    uint32_t line_id = payload.value("line_id", 0U);
    std::string st_city = payload.value("station_city", "");

    auto& ds = DataStore::instance();
    auto* train = ds.getTrain(tid);
    if (!train) {
        result.error = "列车 " + tid + " 不存在";
        return result;
    }

    uint32_t st_id = ds.cityToStationId(st_city);
    if (st_id == 0) {
        result.error = "站点 " + st_city + " 不存在";
        return result;
    }
    auto* new_station = ds.getStation(st_id);
    if (!new_station) {
        result.error = "站点数据缺失";
        return result;
    }

    // 3. 用线路站点顺序精确定位插入位置（而非总是第一个区间）
    int insert_idx = findStopInsertIndex(*train, line_id, st_city, ds);
    if (insert_idx < 0) {
        result.error = "未找到该线路在列车停站中的区间";
        return result;
    }

    // 4. 构建插入后的临时列车，复用 checkTrain() 做时间校验 + 冲突检测
    Stop new_stop;
    new_stop.station_id = st_id;
    new_stop.line_id = line_id;
    new_stop.arrival = arrival;
    new_stop.departure = departure;
    new_stop.stop_type = (arrival == departure) ? 2 : 1;  // 通过/停靠

    Train modified = *train;
    modified.stops.insert(modified.stops.begin() + insert_idx, new_stop);

    auto cr = TrainManager::instance().checkTrain(modified, false);
    if (!cr.valid) {
        result.error = cr.error;
        return result;
    }

    // 5. 速度校验：用 timeDiff() + haversineDist()，限速 = min(列车设计时速, 线路设计时速)
    const auto& prev_stop = modified.stops[insert_idx - 1];
    const auto& next_stop = modified.stops[insert_idx + 1];
    auto* prev_station = ds.getStation(prev_stop.station_id);
    auto* next_station = ds.getStation(next_stop.station_id);
    if (!prev_station || !next_station) {
        result.error = "相邻站点数据缺失";
        return result;
    }

    int train_max = getTrainMaxSpeed(tid);
    int line_max = 300;
    for (const auto& ln : ds.getAllLines()) {
        if (ln.id == line_id) { line_max = ln.max_speed_kmh; break; }
    }
    int speed_limit = std::min(train_max, line_max);

    auto checkSegment = [speed_limit, &result](const Station& from, const Station& to,
                            int time_from, int time_to) -> bool {
        double dist = haversineDist(from, to);
        int mins = timeDiff(time_from, time_to);
        if (dist <= 0 || mins <= 0) return true;
        int speed = static_cast<int>(dist / (mins / 60.0));
        if (speed <= speed_limit) return true;
        result.error = from.name + " → " + to.name
            + " 段时速 " + std::to_string(speed)
            + " km/h 超限（限速 " + std::to_string(speed_limit) + " km/h）";
        return false;
    };

    if (!checkSegment(*prev_station, *new_station, prev_stop.departure, arrival))
        return result;
    if (next_stop.arrival > 0
        && !checkSegment(*new_station, *next_station, departure, next_stop.arrival)) {
        return result;
    }

    // 6. 全部通过，写入 payload
    payload["arrival"] = arrival;
    payload["departure"] = departure;
    it->payload = payload.dump();
    saveApprovals();
    result.success = true;
    Logger::instance().info("Stop time validated for " + tid
        + " at " + st_city + ": arr=" + std::to_string(arrival)
        + " dep=" + std::to_string(departure)
        + " speed_limit=" + std::to_string(speed_limit));
    return result;
}

// ── 审批 ──

ApprovalService::ApproveResult ApprovalService::approve(
    const std::string& approval_id, const std::string& approver_id) {

    ApproveResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. CAS 锁
    if (cas_lock_.test_and_set()) {
        result.error = "审批操作进行中，请稍后重试";
        return result;
    }

    // 2. 找审批
    auto it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) { return a.id == approval_id; });
    if (it == approvals_.end()) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }

    if (it->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "该审批已被处理";
        return result;
    }

    // 3. 四眼原则
    if (it->submitter_id == approver_id) {
        cas_lock_.clear();
        result.error = "不能审批自己提交的申请";
        return result;
    }

    // 4. 执行变更（CREATE/DELETE 含冲突校验，ADJUST 由 updateTrain 内部原子完成）+ 持久化
    try {
        json payload = json::parse(it->payload);
        std::string tid = payload.value("id", "");
        auto& ds = DataStore::instance();

        if (it->type == ApprovalType::CREATE_TRAIN) {
            // 列车已在提交时写入 DataStore（PENDING），审批通过 → 改为 ACTIVE + 入占用
            auto* train = ds.getTrainMutable(tid);
            if (!train) {
                cas_lock_.clear();
                result.error = "列车 " + tid + " 不存在（可能已被删除）";
                return result;
            }
            if (train->status != TrainStatus::PENDING) {
                cas_lock_.clear();
                result.error = "列车 " + tid + " 状态不是待审批（当前：" + std::to_string(static_cast<int>(train->status)) + "）";
                return result;
            }

            // 二次校验：仅冲突检测（ID/日期/停站在提交时已校验，此处只防并发冲突）
            auto conflicts = TrainManager::instance().detectConflicts(*train);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间重叠";
                return result;
            }

            train->status = TrainStatus::ACTIVE;
            TrainManager::instance().addToOccupancy(*train);
            ds.saveTrains();
            WalWriter::instance().append("TRAIN_CREATE",
                json(*train).dump());
            result.train_id = tid;
        } else if (it->type == ApprovalType::DELETE_TRAIN) {
            TrainManager::instance().deleteTrain(tid);
            ds.saveTrains();
            WalWriter::instance().append("TRAIN_DELETE",
                json({{"id", tid}}).dump());
            result.train_id = tid;
        } else if (it->type == ApprovalType::ADJUST_SCHEDULE) {
            // 从 payload 中读取完整新数据，合并到当前列车
            auto* train = ds.getTrain(tid);
            if (!train) {
                cas_lock_.clear();
                result.error = "列车 " + tid + " 不存在（可能已被删除）";
                return result;
            }
            Train updated = *train;
            updated.stops = payload["stops"].get<std::vector<Stop>>();

            // updateTrain 内部原子执行：移除旧占用 → 冲突检测 → 写入新数据+占用
            auto ur = TrainManager::instance().updateTrain(tid, updated);
            if (!ur.success) {
                cas_lock_.clear();
                result.error = ur.error;
                return result;
            }
            ds.saveTrains();
            result.train_id = tid;
        } else if (it->type == ApprovalType::STOP_INSERT) {
            std::string tid = payload.value("train_id", "");
            uint32_t line_id = payload.value("line_id", 0U);
            auto* train = ds.getTrainMutable(tid);
            if (!train) {
                cas_lock_.clear();
                result.error = "列车 " + tid + " 不存在";
                return result;
            }
            // O(1) 城市名→站ID
            std::string st_city = payload.value("station_city", "");
            uint32_t st_id = ds.cityToStationId(st_city);
            if (st_id == 0) {
                cas_lock_.clear();
                result.error = "站点 " + st_city + " 不存在";
                return result;
            }
            // 用线路站点顺序精确定位插入位置（而非总是第一个区间）
            int insert_idx = findStopInsertIndex(*train, line_id, st_city, ds);
            if (insert_idx < 0) {
                cas_lock_.clear();
                result.error = "未找到该线路在列车停站中的区间";
                return result;
            }
            // 构建新停站
            Stop new_stop;
            new_stop.station_id = st_id;
            new_stop.line_id = line_id;
            new_stop.stop_type = 2;  // 默认通过
            if (payload.contains("arrival") && payload.contains("departure")) {
                new_stop.arrival = payload.value("arrival", 0);
                new_stop.departure = payload.value("departure", 0);
                new_stop.stop_type = (new_stop.arrival == new_stop.departure) ? 2 : 1;
            } else {
                // 通过：到达=发车=前站发车+后站到达的中点
                int prev_dep = train->stops[insert_idx - 1].departure;
                int next_arr = train->stops[insert_idx].arrival;
                int mid = (prev_dep > 0 && next_arr > 0)
                    ? (prev_dep + next_arr) / 2 : prev_dep;
                new_stop.arrival = mid;
                new_stop.departure = mid;
            }

            // 二次冲突校验：模拟插入后的运行图
            Train modified = *train;
            modified.stops.insert(modified.stops.begin() + insert_idx, new_stop);
            auto conflicts = TrainManager::instance().detectConflicts(modified);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间重叠";
                return result;
            }

            // 插入并重建占用表
            train->stops = modified.stops;
            TrainManager::instance().adjustSchedule(tid, train->stops);
            ds.saveTrains();
            result.train_id = tid;
        }

        it->status = ApprovalState::APPROVED;
        it->approver_id = approver_id;
        it->decided_at = nowIso();
        saveApprovals();
        result.success = true;
        Logger::instance().info("Approval approved: " + approval_id);
    } catch (const std::exception& e) {
        cas_lock_.clear();
        result.error = std::string("审批生效失败: ") + e.what();
        return result;
    }

    cas_lock_.clear();
    return result;
}

ApprovalService::RejectResult ApprovalService::reject(
    const std::string& approval_id, const std::string& approver_id,
    const std::string& comment) {

    RejectResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    if (cas_lock_.test_and_set()) {
        result.error = "审批操作进行中，请稍后重试";
        return result;
    }

    auto it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) { return a.id == approval_id; });
    if (it == approvals_.end()) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }
    if (it->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "该审批已被处理";
        return result;
    }
    if (it->submitter_id == approver_id) {
        cas_lock_.clear();
        result.error = "不能审批自己提交的申请";
        return result;
    }

    it->status = ApprovalState::REJECTED;
    it->approver_id = approver_id;
    it->comment = comment;
    it->decided_at = nowIso();
    archivePendingTrain(*it);
    saveApprovals();
    result.success = true;
    Logger::instance().info("Approval rejected: " + approval_id);

    cas_lock_.clear();
    return result;
}

ApprovalService::WithdrawResult ApprovalService::withdraw(
    const std::string& approval_id, const std::string& submitter_id) {
    WithdrawResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // CAS 锁（与 approve/reject 一致，防止并发状态变更）
    if (cas_lock_.test_and_set()) {
        result.error = "审批操作进行中，请稍后重试";
        return result;
    }

    auto it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) { return a.id == approval_id; });
    if (it == approvals_.end()) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }
    if (it->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "只能撤回待审批的申请";
        return result;
    }
    if (it->submitter_id != submitter_id) {
        cas_lock_.clear();
        result.error = "只能撤回自己的提交";
        return result;
    }

    it->status = ApprovalState::WITHDRAWN;
    it->decided_at = nowIso();
    archivePendingTrain(*it);
    saveApprovals();
    result.success = true;
    Logger::instance().info("Approval withdrawn: " + approval_id);

    cas_lock_.clear();
    return result;
}

// ── 查询 ──

std::vector<ApprovalRequest> ApprovalService::getApprovals(
    std::optional<ApprovalState> status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!status) return approvals_;

    std::vector<ApprovalRequest> result;
    for (const auto& a : approvals_) {
        if (a.status == *status) result.push_back(a);
    }
    return result;
}

const ApprovalRequest* ApprovalService::getApproval(const std::string& id) const {
    for (const auto& a : approvals_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}
