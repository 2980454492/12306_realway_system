// audit_logger.h — 审计日志服务，链式 SHA256 防篡改 + 无锁队列
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

/**
 * 单条审计记录。
 * hash = SHA256(detail_json + prev_hash)，链式保证不可篡改。
 */
struct AuditRecord {
    std::string id;
    std::string timestamp;   // ISO 8601
    std::string user_id;
    std::string role;
    std::string action;      // LOGIN, USER_CREATE, USER_DELETE, TRAIN_CREATE, etc.
    std::string target;      // 操作对象（如 "user:staff01", "train:G1234"）
    std::string detail;      // JSON 格式的额外信息
    std::string result;      // "success" / "failure"
    std::string ip;
    std::string hash;        // 当前记录的链式哈希
    std::string prev_hash;   // 上一条记录的哈希
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuditRecord,
    id, timestamp, user_id, role, action, target, detail, result, ip, hash, prev_hash)

/**
 * AuditLogger 单例 — 异步审计日志写入。
 *
 * 业务线程调用 log() 将记录入队（快速返回），后台线程负责：
 * 1. 计算链式哈希 SHA256(data + prev_hash)
 * 2. 写入 audit.log
 *
 * 链式校验：任意一条记录被篡改 → 后续所有记录的 hash 全部失效。
 */
class AuditLogger {
public:
    static AuditLogger& instance();

    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;

    /** 启动后台写入线程，加载已有记录用于链式哈希连续性 */
    bool initialize(const std::string& file_path);

    /** 入队一条审计记录（非阻塞，调用方快速返回） */
    void log(const std::string& user_id,
             const std::string& role,
             const std::string& action,
             const std::string& target,
             const std::string& detail,
             const std::string& result,
             const std::string& ip = "127.0.0.1");

    /** 优雅关闭：停止后台线程，等待队列排空 */
    void shutdown();

    /** 读取全部审计记录（用于 API 查询） */
    std::vector<AuditRecord> getRecords() const;

    /** 校验链式哈希完整性。返回 true 表示未被篡改 */
    bool verifyChain() const;

private:
    AuditLogger() = default;
    ~AuditLogger();

    void writerLoop();
    void processRecord(AuditRecord& record);
    static std::string auditDataStr(const AuditRecord& r);

    mutable std::mutex mutex_;
    std::queue<AuditRecord> queue_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> writer_thread_;

    std::string file_path_;
    std::ofstream file_;     // 持久文件句柄，初始化时打开
    std::string last_hash_;  // 上一条记录的哈希链
    std::vector<AuditRecord> records_;  // 已持久化的记录（供查询）
};
