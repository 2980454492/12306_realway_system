// wal_writer.h — WAL 预写日志：写操作先 append+fsync → 改内存，崩溃恢复时重放
#pragma once

#include <string>
#include <mutex>
#include <fstream>
#include <functional>

/**
 * WalWriter 单例 — 线程安全的 WAL 预写日志。
 *
 * 写路径：append() → 写一行 JSON + fsync → 返回
 * 崩溃恢复：recover() → 逐行读取 WAL → 回调 apply() → checkpoint
 *
 * 每条记录是一行 JSON：{"op":"...","ts":unix_timestamp,"data":{...}}
 */
class WalWriter {
public:
    static WalWriter& instance();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    /** 打开 WAL 文件（append 模式），启动时调用 */
    bool initialize(const std::string& path);

    /** 追加一条操作记录（JSON 行 + fsync） */
    void append(const std::string& op, const std::string& data_json);

    /** 崩溃恢复：逐行读取 WAL，对每条记录调用回调。
     *  回调签名为 void(const std::string& op, const std::string& data_json)。
     *  重放完成后执行 checkpoint。 */
    void recover(std::function<void(const std::string&, const std::string&)> apply);

    /** 截断 WAL（所有数据已通过快照持久化后调用） */
    void checkpoint();

    /** 刷新并关闭 WAL 文件（优雅关闭时调用） */
    void shutdown();

private:
    WalWriter() = default;
    ~WalWriter();

    std::mutex mutex_;
    std::ofstream file_;
    std::string path_;
};
