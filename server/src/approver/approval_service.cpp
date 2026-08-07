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
                                    const std::string& payload, ApprovalState initial_status) {
    std::lock_guard<std::mutex> lock(mutex_);

    ApprovalRequest req;
    req.id = generateUuid();
    req.type = type;
    req.submitter_id = submitter_id;
    req.payload = payload;
    req.status = initial_status;
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
        std::string tid = payload.value("train_id", "");
        Train* train = DataStore::instance().getTrain(tid);
        if (train && train->status == TrainStatus::PENDING) {
            train->status = TrainStatus::ARCHIVED;
            DataStore::instance().saveTrains();
        }
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("archivePendingTrain failed: ") + e.what());
    }
}

namespace {

/** 线路变更类型判断：加站、改站、删站 */
inline bool isLineChangeType(ApprovalType t) {
    return t == ApprovalType::STOP_INSERT
        || t == ApprovalType::STOP_REPLACE
        || t == ApprovalType::STOP_REMOVE;
}

/** 从 stops 中移除指定站点（按 station_id + line_id 精确匹配），返回移除位置，-1 表示未找到 */
int removeStopByStation(std::vector<Stop>& stops, uint32_t station_id, uint32_t line_id) {
    for (size_t i = 0; i < stops.size(); i++) {
        if (stops[i].station_id == station_id && stops[i].line_id == line_id) {
            stops.erase(stops.begin() + i);
            return static_cast<int>(i);
        }
    }
    return -1;
}

/** 检查两站间的实际时速是否超过限速。返回空 = 通过，非空 = 错误信息 */
std::string checkSpeedLimit(const Station& from, const Station& to,
                             int time_from, int time_to, int speed_limit) {
    double dist = haversineDist(from, to);
    int mins = timeDiff(time_from, time_to);
    if (dist <= 0 || mins <= 0) return "";
    int speed = static_cast<int>(dist / (mins / 60.0));
    if (speed <= speed_limit) return "";
    return from.name + " → " + to.name
        + " 段时速 " + std::to_string(speed)
        + " km/h 超限（限速 " + std::to_string(speed_limit) + " km/h）";
}

}  // namespace

// ── 停站时间 ──
ApprovalService::UpdateStopTimeResult ApprovalService::updateStopTime(
    const std::string& approval_id, int arrival, int departure,
    const std::string& effective_date, const std::string& staff_id) {

    UpdateStopTimeResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // 0. 生效日期须 ≥ MAX_ADVANCE_DAYS+1 天后
    if (effective_date.empty()) {
        result.error = "请选择生效日期";
        return result;
    }
    if (!isAtLeastDaysAhead(effective_date, MAX_ADVANCE_DAYS + 1)) {
        result.error = "生效日期须至少 " + std::to_string(MAX_ADVANCE_DAYS + 1)
            + " 天后（当前选择 " + effective_date + "）";
        return result;
    }

    // 1. 找到审批（支持三种线路变更类型 + DRAFT→SUBMITTED 过渡）
    std::vector<ApprovalRequest>::iterator it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) {
            return a.id == approval_id && isLineChangeType(a.type);
        });
    if (it == approvals_.end()) {
        result.error = "审批不存在或类型不匹配";
        return result;
    }

    // 2. 解析载荷（插入位置 + 前后站信息已在创建审批时算好）
    json payload = json::parse(it->payload);
    std::string train_id = payload.value("train_id", "");
    uint32_t line_id = payload.value("line_id", 0U);
    std::string station_name = payload.value("station_name", "");
    int insert_idx = payload.value("insert_index", -1);

    DataStore& ds = DataStore::instance();
    Train* train = ds.getTrain(train_id);
    if (!train) {
        result.error = "列车 " + train_id + " 不存在";
        return result;
    }

    uint32_t station_id = ds.stationNameToId(station_name);
    if (station_id == 0) {
        result.error = "站点 " + station_name + " 不存在";
        return result;
    }
    Station* new_station = ds.getStation(station_id);
    if (!new_station) {
        result.error = "站点数据缺失";
        return result;
    }
    bool is_remove = (it->type == ApprovalType::STOP_REMOVE);
    if (!is_remove && insert_idx < 0) {
        result.error = "未找到该线路在列车停站中的区间";
        return result;
    }

    // 3. 构建临时列车并做时间/速度/冲突校验
    Train new_train = *train;
    new_train.valid_from = effective_date;

    if (is_remove) {
        // 删站：从 stops 中移除该站
        std::vector<Stop>::iterator stop_it = std::find_if(new_train.stops.begin(), new_train.stops.end(),
            [station_id, line_id](const Stop& s) {
                return s.station_id == station_id && s.line_id == line_id;
            });
        if (stop_it == new_train.stops.end()) {
            result.error = "列车 " + train_id + " 中未找到站点 " + station_name;
            return result;
        }
        new_train.stops.erase(stop_it);
    } else {
        // 改站：先移除旧站，再插入新站
        std::string replace_name = payload.value("replace_station_name", "");
        if (!replace_name.empty()) {
            uint32_t old_id = ds.stationNameToId(replace_name);
            int removed_at = removeStopByStation(new_train.stops, old_id, line_id);
            if (removed_at >= 0 && removed_at < insert_idx)
                insert_idx--;
        }

        // 加站/改站：插入新站点
        Stop new_stop;
        new_stop.station_id = station_id;
        new_stop.line_id = line_id;
        new_stop.arrival = arrival;
        new_stop.departure = departure;
        new_stop.stop_type = (arrival == departure) ? StopType::PASS : StopType::STOP;
        new_train.stops.insert(new_train.stops.begin() + insert_idx, new_stop);
    }

    TrainManager::CheckResult cr = TrainManager::instance().checkTrain(new_train, false);
    if (!cr.valid) {
        result.error = cr.error;
        return result;
    }

    // 4. 非删站时做速度校验
    if (!is_remove) {
        const Stop& prev_stop = new_train.stops[insert_idx - 1];
        const Stop& next_stop = new_train.stops[insert_idx + 1];
        Station* prev_station = ds.getStation(prev_stop.station_id);
        Station* next_station = ds.getStation(next_stop.station_id);
        if (!prev_station || !next_station) {
            result.error = "相邻站点数据缺失";
            return result;
        }
        int speed_limit = ds.getLine(line_id)->max_speed_kmh;
        std::string err = checkSpeedLimit(*prev_station, *new_station, prev_stop.departure, arrival, speed_limit);
        if (!err.empty()) { result.error = err; return result; }
        if (next_stop.arrival > 0) {
            err = checkSpeedLimit(*new_station, *next_station, departure, next_stop.arrival, speed_limit);
            if (!err.empty()) { result.error = err; return result; }
        }
    }

    // 5. 全部通过，写入 payload + DRAFT→SUBMITTED
    if (!is_remove) {
        payload["arrival"] = arrival;
        payload["departure"] = departure;
    }
    payload["effective_date"] = effective_date;
    if (!staff_id.empty())
        payload["staff_id"] = staff_id;
    it->payload = payload.dump();
    it->status = ApprovalState::SUBMITTED;
    saveApprovals();
    result.success = true;
    Logger::instance().info("Stop time submitted for " + train_id
        + " type=" + std::to_string(static_cast<int>(it->type)));
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
    ApprovalRequest* req = getApproval(approval_id);
    if (!req) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }

    if (req->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "该审批已被处理";
        return result;
    }

    // 3. 四眼原则
    if (req->submitter_id == approver_id) {
        cas_lock_.clear();
        result.error = "不能审批自己提交的申请";
        return result;
    }

    // 4. 执行变更（CREATE/DELETE 含冲突校验，ADJUST 由 updateTrain 内部原子完成）+ 持久化
    try {
        json payload = json::parse(req->payload);
        std::string train_id = payload.value("train_id", "");
        DataStore& ds = DataStore::instance();
        Train* train = ds.getTrain(train_id);
        if (!train) {
            cas_lock_.clear();
            result.error = "列车 " + train_id + " 不存在（可能已被删除）";
            return result;
        }

        uint32_t line_id = payload.value("line_id", 0U);
        std::string station_name = payload.value("station_name", "");
        uint32_t station_id = ds.stationNameToId(station_name);
        if (req->type == ApprovalType::CREATE_TRAIN) {
            // 列车已在提交时写入 DataStore（PENDING），审批通过 → 改为 ACTIVE + 入占用
            if (train->status != TrainStatus::PENDING) {
                cas_lock_.clear();
                result.error = "列车 " + train_id + " 状态不是待审批（当前：" + std::to_string(static_cast<int>(train->status)) + "）";
                return result;
            }

            // 二次校验：仅冲突检测（ID/日期/停站在提交时已校验，此处只防并发冲突）
            std::vector<TrainManager::ConflictDetail> conflicts = TrainManager::instance().detectConflicts(*train);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间重叠";
                return result;
            }

            train->status = TrainStatus::ACTIVE;
            TrainManager::instance().addToOccupancy(*train);
            ds.saveTrains();
            WalWriter::instance().append("TRAIN_CREATE", json(*train).dump());
            result.train_id = train_id;
        } else if (req->type == ApprovalType::DELETE_TRAIN) {
            std::string delete_date = payload.value("delete_date", "");
            if (!delete_date.empty() && delete_date > todayStr()) {
                // 未来删除：设置 valid_until，列车保留 ACTIVE 直到该日期
                train->valid_until = delete_date;
                ds.saveTrains();
                Logger::instance().info("Train " + train_id + " scheduled for deletion on " + delete_date);
            } else {
                // 立即删除（无日期或日期已到）
                TrainManager::instance().deleteTrain(train_id);
                ds.saveTrains();
            }
            WalWriter::instance().append("TRAIN_DELETE", json({{"train_id", train_id}}).dump());
            result.train_id = train_id;
        } else if (req->type == ApprovalType::ADJUST_SCHEDULE) {
            // 从 payload 中读取完整新数据，合并到当前列车
            Train new_train = *train;
            new_train.stops = payload["stops"].get<std::vector<Stop>>();

            // updateTrain 内部原子执行：移除旧占用 → 冲突检测 → 写入新数据+占用
            TrainManager::UpdateResult ur = TrainManager::instance().updateTrain(train_id, new_train);
            if (!ur.success) {
                cas_lock_.clear();
                result.error = ur.error;
                return result;
            }
            ds.saveTrains();
            result.train_id = train_id;
        } else if (req->type == ApprovalType::STOP_INSERT) {
            
            // 从 payload 读取插入位置（创建审批时已算好）
            int insert_idx = payload.value("insert_index", -1);
            if (insert_idx < 0) {
                cas_lock_.clear();
                result.error = "未找到该线路在列车停站中的区间";
                return result;
            }
            // 构建新停站
            Stop new_stop;
            new_stop.station_id = station_id;
            new_stop.line_id = line_id;
            new_stop.stop_type = StopType::PASS;  // 默认通过
            if (payload.contains("arrival") && payload.contains("departure")) {
                new_stop.arrival = payload.value("arrival", 0);
                new_stop.departure = payload.value("departure", 0);
                new_stop.stop_type = (new_stop.arrival == new_stop.departure) ? StopType::PASS : StopType::STOP;
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
            Train new_train = *train;
            new_train.stops.insert(new_train.stops.begin() + insert_idx, new_stop);
            std::vector<TrainManager::ConflictDetail> conflicts = TrainManager::instance().detectConflicts(new_train);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间重叠";
                return result;
            }

            // 插入并重建占用表
            train->stops = new_train.stops;
            // 应用生效日期：若 payload 中有 effective_date 且晚于当前 valid_from，则更新
            std::string eff_date = payload.value("effective_date", "");
            if (!eff_date.empty() && eff_date > train->valid_from)
                train->valid_from = eff_date;
            TrainManager::instance().adjustSchedule(train_id, train->stops);
            ds.saveTrains();
            result.train_id = train_id;
        } else if (req->type == ApprovalType::STOP_REPLACE) {
            // 线路改站：替换旧站为新站
            std::string train_id = payload.value("train_id", "");
            uint32_t line_id = payload.value("line_id", 0U);
            std::string station_name = payload.value("station_name", "");
            uint32_t station_id = ds.stationNameToId(station_name);
            std::string replace_name = payload.value("replace_station_name", "");
            int insert_idx = payload.value("insert_index", -1);
            if (station_id == 0 || insert_idx < 0) {
                cas_lock_.clear();
                result.error = "改站参数缺失";
                return result;
            }

            // 先移除旧站
            uint32_t old_id = ds.stationNameToId(replace_name);
            int removed_at = removeStopByStation(train->stops, old_id, line_id);
            if (removed_at >= 0 && removed_at < insert_idx)
                insert_idx--;

            // 插入新站
            Stop new_stop;
            new_stop.station_id = station_id;
            new_stop.line_id = line_id;
            new_stop.arrival = payload.value("arrival", 0);
            new_stop.departure = payload.value("departure", 0);
            new_stop.stop_type = (new_stop.arrival == new_stop.departure) ? StopType::PASS : StopType::STOP;
            train->stops.insert(train->stops.begin() + insert_idx, new_stop);

            Train new_train = *train;
            std::vector<TrainManager::ConflictDetail> conflicts = TrainManager::instance().detectConflicts(new_train);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间重叠";
                return result;
            }

            std::string eff_date = payload.value("effective_date", "");
            if (!eff_date.empty() && eff_date > train->valid_from)
                train->valid_from = eff_date;
            TrainManager::instance().adjustSchedule(train_id, train->stops);
            ds.saveTrains();
            result.train_id = train_id;
        } else if (req->type == ApprovalType::STOP_REMOVE) {
            // 线路删除车站 → 从受影响列车中移除该站

            // 按 station_id + line_id 精确匹配要移除的 stop
            std::vector<Stop>::iterator stop_it = std::find_if(train->stops.begin(), train->stops.end(),
                [station_id, line_id](const Stop& s) {
                    return s.station_id == station_id && s.line_id == line_id;
                });
            if (stop_it == train->stops.end()) {
                cas_lock_.clear();
                result.error = "列车 " + train_id + " 中未找到站点 " + station_name;
                return result;
            }
            train->stops.erase(stop_it);
            TrainManager::instance().adjustSchedule(train_id, train->stops);
            ds.saveTrains();
            Logger::instance().info("STOP_REMOVE: removed " + station_name + " from " + train_id);
            result.train_id = train_id;
        }

        req->status = ApprovalState::APPROVED;
        req->approver_id = approver_id;
        req->decided_at = nowIso();
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

    ApprovalRequest* req = getApproval(approval_id);
    if (!req) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }
    if (req->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "该审批已被处理";
        return result;
    }
    if (req->submitter_id == approver_id) {
        cas_lock_.clear();
        result.error = "不能审批自己提交的申请";
        return result;
    }

    req->status = ApprovalState::REJECTED;
    req->approver_id = approver_id;
    req->comment = comment;
    req->decided_at = nowIso();
    archivePendingTrain(*req);
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

    ApprovalRequest* req = getApproval(approval_id);
    if (!req) {
        cas_lock_.clear();
        result.error = "审批不存在";
        return result;
    }
    if (req->status != ApprovalState::SUBMITTED) {
        cas_lock_.clear();
        result.error = "只能撤回待审批的申请";
        return result;
    }
    if (req->submitter_id != submitter_id) {
        cas_lock_.clear();
        result.error = "只能撤回自己的提交";
        return result;
    }

    req->status = ApprovalState::WITHDRAWN;
    req->decided_at = nowIso();
    archivePendingTrain(*req);
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
    for (const ApprovalRequest& a : approvals_) {
        if (a.status == *status) result.push_back(a);
    }
    return result;
}

ApprovalRequest* ApprovalService::getApproval(const std::string& id) {
    for (ApprovalRequest& a : approvals_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}
