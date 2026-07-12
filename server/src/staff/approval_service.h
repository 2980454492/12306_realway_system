// approval_service.h — 审批流服务，状态机 + CAS 锁 + 四眼原则 + 二次冲突校验
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <atomic>

/**
 * ApprovalService 单例 — 管理审批生命周期。
 * 四眼原则：submitter_id != approver_id。
 * CAS 锁：std::atomic_flag 防止多人同时审批同一申请。
 * 二次冲突校验：审批通过前再次执行冲突检测，对比提交快照与当前状态。
 */
class ApprovalService {
public:
    static ApprovalService& instance();

    ApprovalService(const ApprovalService&) = delete;
    ApprovalService& operator=(const ApprovalService&) = delete;

    /** 从目录加载审批数据 */
    bool initialize(const std::string& data_dir);

    // ── 提交 ──

    /** 职工提交审批申请，返回审批 ID */
    std::string submit(ApprovalType type, const std::string& submitter_id,
                      const std::string& payload, const std::string& snapshot);

    // ── 审批 ──

    /** 审批通过（四眼原则 + CAS + 二次冲突校验） */
    struct ApproveResult {
        bool success = false;
        std::string error;
        std::string train_id;  // 审批通过的列车 ID（前端展示用）
    };
    ApproveResult approve(const std::string& approval_id, const std::string& approver_id);

    /** 审批驳回 */
    struct RejectResult {
        bool success = false;
        std::string error;
    };
    RejectResult reject(const std::string& approval_id, const std::string& approver_id,
                       const std::string& comment);

    // ── 查询 ──

    /** 按状态筛选审批列表 */
    std::vector<ApprovalRequest> getApprovals(
        std::optional<ApprovalState> status = std::nullopt) const;

    /** 按 ID 查审批 */
    const ApprovalRequest* getApproval(const std::string& id) const;

private:
    ApprovalService() = default;

    void saveApprovals() const;

    // ── 数据 ──
    std::vector<ApprovalRequest> approvals_;
    mutable std::mutex mutex_;
    std::atomic_flag cas_lock_ = ATOMIC_FLAG_INIT;
    std::string data_dir_;
};
