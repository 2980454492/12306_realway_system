// approval_service.cpp — ApprovalService 实现
#include "staff/approval_service.h"
#include "staff/train_manager.h"
#include "data/data_store.h"
#include "core/config.h"
#include "core/utils.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

using json = nlohmann::json;

namespace {
}  // namespace

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
                                    const std::string& payload, const std::string& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    ApprovalRequest req;
    req.id = generateUuid();
    req.type = type;
    req.submitter_id = submitter_id;
    req.payload = payload;
    req.snapshot = snapshot;
    req.submitted_at = nowIso();

    approvals_.push_back(req);
    saveApprovals();
    Logger::instance().info("Approval submitted: " + req.id);
    return req.id;
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

    // 4. 二次冲突校验 + 执行变更 + 持久化
    try {
        json payload = json::parse(it->payload);
        std::string tid = payload.value("id", "");
        auto& ds = DataStore::instance();

        if (it->type == ApprovalType::CREATE_TRAIN) {
            Train train = payload.get<Train>();
            auto conflicts = TrainManager::instance().detectConflicts(train);
            if (!conflicts.empty()) {
                cas_lock_.clear();
                result.error = "二次冲突校验失败：与 " + conflicts[0].train_id + " 在区间冲突";
                return result;
            }
            TrainManager::instance().addTrain(train);
            ds.saveTrains();
            result.train_id = tid;
        } else if (it->type == ApprovalType::DELETE_TRAIN) {
            TrainManager::instance().deleteTrain(tid);
            ds.saveTrains();
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
            if (payload.contains("segments"))
                updated.segments = payload["segments"].get<std::vector<RouteSegment>>();
            if (payload.contains("route_stations"))
                updated.route_stations = payload["route_stations"].get<std::vector<uint32_t>>();
            else {
                updated.route_stations.clear();
                for (const auto& s : updated.stops)
                    updated.route_stations.push_back(s.station_id);
            }

            // updateTrain 内部原子执行：移除旧占用 → 冲突检测 → 写入新数据+占用
            auto ur = TrainManager::instance().updateTrain(tid, updated);
            if (!ur.success) {
                cas_lock_.clear();
                result.error = ur.error;
                return result;
            }
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

    auto it = std::find_if(approvals_.begin(), approvals_.end(),
        [&](const ApprovalRequest& a) { return a.id == approval_id; });
    if (it == approvals_.end()) {
        result.error = "审批不存在";
        return result;
    }
    if (it->status != ApprovalState::SUBMITTED) {
        result.error = "只能撤回待审批的申请";
        return result;
    }
    if (it->submitter_id != submitter_id) {
        result.error = "只能撤回自己的提交";
        return result;
    }

    it->status = ApprovalState::WITHDRAWN;
    it->decided_at = nowIso();
    saveApprovals();
    result.success = true;
    Logger::instance().info("Approval withdrawn: " + approval_id);
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
