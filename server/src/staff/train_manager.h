// train_manager.h — 列车管理服务，新增/删除/调整时刻 + 运行图冲突检测
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>

/** 运行图冲突检测安全裕量（分钟） */
inline constexpr int SAFETY_MARGIN_MINUTES = 5;

/**
 * TrainManager 单例 — 管理列车增删改 + 运行图冲突检测。
 * 新增和调整时刻需走审批流（提交 → ApprovalService 审批 → 回调生效）。
 */
class TrainManager {
public:
    static TrainManager& instance();

    TrainManager(const TrainManager&) = delete;
    TrainManager& operator=(const TrainManager&) = delete;

    /** 从 DataStore 重建区间占用表（读取所有 ACTIVE 列车的停站数据） */
    bool initialize();

    // ── 校验 ──

    /** 校验停站序列合法性（站名在注册站内、时间递增、车次号不重复） */
    struct ValidationResult {
        bool valid = false;
        std::string error;
    };
    ValidationResult validate(const Train& train, bool is_new) const;

    // ── 冲突检测 ──

    /** 检测新增列车与已有列车的运行图冲突。返回冲突详情列表（空 = 无冲突） */
    struct ConflictDetail {
        std::string train_id;      // 与哪个车次冲突
        uint32_t station_a;        // 区间起点
        uint32_t station_b;        // 区间终点
        uint32_t line_id;          // 冲突所在线路
        int conflicting_enter;     // 已有进入时间
        int conflicting_leave;     // 已有离开时间
    };
    std::vector<ConflictDetail> detectConflicts(const Train& train) const;

    // ── 变更 ──

    /** 新增列车（审批通过后调用） */
    bool addTrain(const Train& train);

    /** 删除列车，检查无未出发已售票 */
    struct DeleteResult {
        bool success = false;
        std::string error;
    };
    DeleteResult deleteTrain(const std::string& train_id);

    /** 调整时刻（审批通过后调用），先清理旧占用再写入新占用 */
    bool adjustSchedule(const std::string& train_id, const std::vector<Stop>& new_stops);

    /** 完整更新列车（审批通过后调用），先移除旧占用再检测冲突再写入，原子操作 */
    struct UpdateResult {
        bool success = false;
        std::string error;
    };
    UpdateResult updateTrain(const std::string& train_id, const Train& updated);

    /** 获取全部列车列表 */
    const std::vector<Train>& getAllTrains() const;

private:
    TrainManager() = default;

    void loadOccupancy();
    void saveOccupancy() const;

    // 区间 key = 出发站|到达站|线路ID（方向敏感 + 线路隔离，A→B 与 B→A 不同 key）
    std::string occKey(uint32_t from, uint32_t to, uint32_t line_id) const;
    void addToOccupancy(const Train& train);
    void removeFromOccupancy(const Train& train);

    // ── 数据 ──
    std::map<std::string, std::set<std::pair<int, int>>> occupancy_;  // key → set<(enter, leave)>
    std::map<std::string, std::vector<std::pair<std::string, std::pair<int, int>>>> occ_detail_;
    // key → [(train_id, (enter, leave)), ...]

    mutable std::mutex mutex_;
};
