// seat_inventory.h — 座位库存管理，每 (车次,日期,席位) 细粒度锁
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

/**
 * SeatInventory 单例 — 管理每趟列车每日每席位的座位占用位图。
 * 线程安全：每 (车次, 日期) 一个 shared_mutex。
 * 查票用 shared_lock（读），购票用 unique_lock（写）。
 */
class SeatInventory {
public:
    static SeatInventory& instance();

    SeatInventory(const SeatInventory&) = delete;
    SeatInventory& operator=(const SeatInventory&) = delete;

    /** 获取某车次某日的可用座位数（按席位） */
    SeatConfig getAvailable(const std::string& train_id, const std::string& date);

    /**
     * 预留座位。成功返回分配的 seat_number 列表，失败返回空。
     * 原子操作：检查余票 → 标记为已售 → 写入版本号。
     */
    struct Reservation {
        std::vector<uint16_t> seat_numbers;  // 分配的座位号（1-based）
        bool success = false;
    };
    Reservation reserve(const std::string& train_id, const std::string& date,
                        SeatType seat_type, int count);

    /**
     * 释放座位（退票时调用）。
     * 将指定座位号标记为可售。
     */
    void release(const std::string& train_id, const std::string& date,
                 SeatType seat_type, const std::vector<uint16_t>& seat_numbers);

private:
    SeatInventory() = default;

    // ── 内部数据结构 ──
    struct SeatBitmap {
        std::vector<bool> seats;  // true = 已售
        uint64_t version = 0;     // 乐观锁版本号
    };

    struct TrainInventory {
        std::unordered_map<SeatType, SeatBitmap> seat_maps;  // 按席位存储
        uint64_t version = 0;
    };

    /** 获取或创建某车次某日的库存（惰性初始化） */
    TrainInventory& getOrCreate(const std::string& train_id, const std::string& date);

    /** 确保席位位图已初始化 */
    void ensureSeatType(TrainInventory& inv, SeatType type, uint16_t count);

    // ── 数据 ──
    // key = "train_id|date"，value = 库存
    std::unordered_map<std::string, TrainInventory> inventories_;
    std::unordered_map<std::string, std::unique_ptr<std::shared_mutex>> mutexes_;
    std::shared_mutex global_mutex_;  // 保护 inventories_ 和 mutexes_ 的新增操作

    /** 获取或创建 mutex */
    std::shared_mutex& getMutex(const std::string& key);
};
