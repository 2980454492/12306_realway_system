// audit_logger.cpp — 审计日志实现
#include "core/audit_logger.h"
#include "core/logger.h"
#include "core/utils.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/** SHA256 哈希，返回 hex 字符串 */
std::string sha256Hex(const std::string& input) {
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash,
        reinterpret_cast<const unsigned char*>(input.data()), input.size());
    std::ostringstream oss;
    for (size_t i = 0; i < crypto_hash_sha256_BYTES; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    return oss.str();
}

}  // namespace

// ── 单例 ──

AuditLogger& AuditLogger::instance() {
    static AuditLogger al;
    return al;
}

AuditLogger::~AuditLogger() {
    if (running_.load())
        shutdown();
}

// ── 初始化 ──

bool AuditLogger::initialize(const std::string& file_path) {
    file_path_ = file_path;

    // 确保目录存在
    fs::path parent = fs::path(file_path).parent_path();
    if (!parent.empty() && !fs::exists(parent))
        fs::create_directories(parent);

    // 加载已有记录，恢复 last_hash_ 用于链式连续性
    if (fs::exists(file_path)) {
        std::ifstream in(file_path);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                try {
                    auto record = json::parse(line).get<AuditRecord>();
                    records_.push_back(record);
                    last_hash_ = record.hash;
                } catch (const std::exception&) {
                    // 跳过损坏的行
                }
            }
        }
    }

    // 启动后台写入线程
    running_.store(true);
    writer_thread_ = std::make_unique<std::thread>(&AuditLogger::writerLoop, this);

    Logger::instance().info("AuditLogger started, " +
        std::to_string(records_.size()) + " existing records");
    return true;
}

// ── 写入 ──

void AuditLogger::log(const std::string& user_id,
                       const std::string& role,
                       const std::string& action,
                       const std::string& target,
                       const std::string& detail,
                       const std::string& result,
                       const std::string& ip) {
    AuditRecord record;
    record.id = generateUuid();
    record.timestamp = nowIso();
    record.user_id = user_id;
    record.role = role;
    record.action = action;
    record.target = target;
    record.detail = detail;
    record.result = result;
    record.ip = ip;
    // hash/prev_hash 在 writerLoop 中计算

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(record));
    }
    cv_.notify_one();
}

// ── 后台写入线程 ──

void AuditLogger::writerLoop() {
    while (running_.load()) {
        AuditRecord record;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return !queue_.empty() || !running_.load();
            });
            if (queue_.empty())
                continue;
            record = std::move(queue_.front());
            queue_.pop();
        }

        // 计算链式哈希
        record.prev_hash = last_hash_;
        std::string data_for_hash = record.timestamp + record.user_id +
            record.action + record.target + record.detail + record.result;
        record.hash = sha256Hex(data_for_hash + record.prev_hash);

        // 追加写入文件
        std::ofstream out(file_path_, std::ios::app);
        if (out.is_open()) {
            out << json(record).dump() << '\n';
            out.flush();
            out.close();
        }

        // 更新状态
        last_hash_ = record.hash;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            records_.push_back(record);
        }
    }

    // 关闭前排空剩余队列
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        auto& record = queue_.front();
        record.prev_hash = last_hash_;
        std::string data = record.timestamp + record.user_id +
            record.action + record.target + record.detail + record.result;
        record.hash = sha256Hex(data + record.prev_hash);

        std::ofstream out(file_path_, std::ios::app);
        if (out.is_open()) {
            out << json(record).dump() << '\n';
            out.close();
        }
        last_hash_ = record.hash;
        records_.push_back(record);
        queue_.pop();
    }
}

// ── 关闭 ──

void AuditLogger::shutdown() {
    running_.store(false);
    cv_.notify_all();
    if (writer_thread_ && writer_thread_->joinable())
        writer_thread_->join();
    Logger::instance().info("AuditLogger shut down");
}

// ── 查询 ──

std::vector<AuditRecord> AuditLogger::getRecords() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

// ── 链式校验 ──

bool AuditLogger::verifyChain() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty())
        return true;

    std::string expected_prev;
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        if (r.prev_hash != expected_prev)
            return false;
        std::string data = r.timestamp + r.user_id + r.action +
            r.target + r.detail + r.result;
        std::string expected_hash = sha256Hex(data + r.prev_hash);
        if (r.hash != expected_hash)
            return false;
        expected_prev = r.hash;
    }
    return true;
}
