// audit_logger.cpp — 审计日志实现
#include "audit_service.h"
#include "system/logger.h"
#include "utils.h"

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

// ── 工具 ──

std::string AuditLogger::auditDataStr(const AuditRecord& r) {
    return r.timestamp + r.user_id + r.action + r.target + r.detail + r.result;
}

// ── 初始化 ──

bool AuditLogger::initialize(const std::string& file_path) {
    file_path_ = file_path;

    fs::path parent = fs::path(file_path).parent_path();
    if (!parent.empty() && !fs::exists(parent))
        fs::create_directories(parent);

    // 加载已有记录，恢复链式哈希
    std::ifstream in(file_path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto record = json::parse(line).get<AuditRecord>();
                records_.push_back(record);
                last_hash_ = record.hash;
            } catch (const std::exception&) {}
        }
        in.close();
    }

    // 打开持久文件句柄（避免每条记录重复 fopen/fclose）
    file_.open(file_path, std::ios::app);
    if (!file_.is_open()) {
        Logger::instance().error("Failed to open audit log: " + file_path);
        return false;
    }

    running_.store(true);
    writer_thread_ = std::make_unique<std::thread>(&AuditLogger::writerLoop, this);

    Logger::instance().info("AuditLogger started, " +
        std::to_string(records_.size()) + " existing records");
    return true;
}

// ── 入队 ──

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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(record));
    }
    cv_.notify_one();
}

// ── 处理单条记录（主循环和排空循环共用）──

void AuditLogger::processRecord(AuditRecord& record) {
    record.prev_hash = last_hash_;
    record.hash = sha256Hex(auditDataStr(record) + record.prev_hash);
    if (file_.is_open()) {
        file_ << json(record).dump() << '\n';
        file_.flush();
    }
    last_hash_ = record.hash;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(record);
    }
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
        processRecord(record);
    }

    // 排空剩余队列（逐条取出后释放锁再处理，避免双锁）
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            auto record = std::move(queue_.front());
            queue_.pop();
            lock.unlock();
            processRecord(record);
            lock.lock();
        }
    }
}

// ── 关闭 ──

void AuditLogger::shutdown() {
    running_.store(false);
    cv_.notify_all();
    if (writer_thread_ && writer_thread_->joinable())
        writer_thread_->join();
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
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
        if (r.hash != sha256Hex(auditDataStr(r) + r.prev_hash))
            return false;
        expected_prev = r.hash;
    }
    return true;
}
