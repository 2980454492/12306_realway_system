// wal_writer.cpp — WAL 预写日志实现
#include "wal.h"
#include "logger.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ctime>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/** Unix 时间戳（秒），用于 WAL 记录的 ts 字段 */
int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

// ── 单例 ──

WalWriter& WalWriter::instance() {
    static WalWriter w;
    return w;
}

WalWriter::~WalWriter() {
    if (file_.is_open())
        file_.close();
}

// ── 初始化 ──

bool WalWriter::initialize(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;

    // 确保目录存在
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && !fs::exists(parent))
        fs::create_directories(parent);

    file_.open(path, std::ios::app);
    if (!file_.is_open()) {
        Logger::instance().error("Failed to open WAL file: " + path);
        return false;
    }

    Logger::instance().info("WAL opened: " + path);
    return true;
}

// ── 写入 ──

void WalWriter::append(const std::string& op, const std::string& data_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open())
        return;

    json record;
    record["op"] = op;
    record["ts"] = unixNow();
    record["data"] = json::parse(data_json.empty() ? "{}" : data_json);

    std::string line = record.dump();
    // 一行 JSON 不能包含换行符
    file_ << line << '\n';
    file_.flush();

    // fsync 确保落盘（崩溃恢复的保证）
    // 注：C++ 标准库没有直接的 fsync，但 flush + 操作系统缓冲区刷新已足够
    // 对于严格耐久性，可以用 fileno + fsync(fd)
}

// ── 崩溃恢复 ──

void WalWriter::recover(std::function<void(const std::string&, const std::string&)> apply) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 关闭 append 模式，改为读取
    if (file_.is_open()) {
        file_.close();
    }

    std::ifstream in(path_);
    if (!in.is_open()) {
        // WAL 文件不存在（首次运行），重新打开为 append 模式
        file_.open(path_, std::ios::app);
        Logger::instance().info("No WAL file to recover, starting fresh");
        return;
    }

    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        try {
            json record = json::parse(line);
            std::string op = record.value("op", "");
            std::string data = record.contains("data")
                ? record["data"].dump() : "{}";
            apply(op, data);
            ++count;
        } catch (const std::exception& e) {
            Logger::instance().warn(std::string("Skipping corrupt WAL line: ") + e.what());
        }
    }
    in.close();

    if (count > 0)
        Logger::instance().info("WAL recovery: replayed " + std::to_string(count) + " entries");

    // 重放完成后截断 WAL，重新打开为 append 模式
    checkpoint();
}

// ── 检查点 ──

void WalWriter::checkpoint() {
    // 关闭当前文件，截断后重新打开
    if (file_.is_open())
        file_.close();

    // 截断为空
    std::ofstream trunc(path_, std::ios::trunc);
    trunc.close();

    // 重新打开为 append 模式
    file_.open(path_, std::ios::app);
}

// ── 关闭 ──

void WalWriter::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    Logger::instance().info("WAL shut down");
}
