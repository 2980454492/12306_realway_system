// logger.cpp — Logger 单例实现
#include "logger.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLogFile(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 确保父目录存在
    namespace fs = std::filesystem;
    fs::path path(file_path);
    if (auto parent = path.parent_path(); !parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }

    if (file_.is_open()) {
        file_.close();
    }
    file_.open(file_path, std::ios::app);
    file_open_ = file_.is_open();
}

void Logger::info(const std::string& message) {
    write("INFO", message);
}

void Logger::warn(const std::string& message) {
    write("WARN", message);
}

void Logger::error(const std::string& message) {
    write("ERROR", message);
}

// ── 内部实现 ──

void Logger::write(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string line = "[" + timestamp() + "] [" + level + "] " + message;

    // 控制台输出：ERROR → stderr，其余 → stdout
    if (level == "ERROR") {
        std::cerr << line << std::endl;
    } else {
        std::cout << line << std::endl;
    }

    // 文件输出
    if (file_open_) {
        file_ << line << std::endl;
        file_.flush();  // 每条日志立即落盘，崩溃不丢
    }
}

std::string Logger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);  // 线程安全版本

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}
