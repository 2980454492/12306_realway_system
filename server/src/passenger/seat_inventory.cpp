// seat_inventory.cpp — SeatInventory 实现
#include "passenger/seat_inventory.h"
#include "data/data_store.h"
#include "core/logger.h"

#include <algorithm>

namespace {

// 生成 (车次, 日期) 组合 key
std::string makeKey(const std::string& train_id, const std::string& date) {
    return train_id + "|" + date;
}
}  // namespace

// ── 单例 ──

SeatInventory& SeatInventory::instance() {
    static SeatInventory inv;
    return inv;
}

// ── 查询可用座位 ──

SeatConfig SeatInventory::getAvailable(const std::string& train_id, const std::string& date) {
    std::string key = makeKey(train_id, date);
    auto& mtx = getMutex(key);
    std::shared_lock<std::shared_mutex> lock(mtx);

    auto& inv = getOrCreate(train_id, date);

    SeatConfig config;
    for (auto& [type, bitmap] : inv.seat_maps) {
        uint16_t available = 0;
        for (bool sold : bitmap.seats) {
            if (!sold) ++available;
        }
        switch (type) {
            case SeatType::BUSINESS:     config.business_seats = available; break;
            case SeatType::FIRST:        config.first_seats = available; break;
            case SeatType::SECOND:       config.second_seats = available; break;
            case SeatType::HARD_SLEEPER: config.hard_sleeper = available; break;
            case SeatType::HARD_SEAT:    config.hard_seat = available; break;
            case SeatType::NO_SEAT:      config.no_seat = available; break;
        }
    }
    return config;
}

// ── 预留座位 ──

SeatInventory::Reservation SeatInventory::reserve(
    const std::string& train_id, const std::string& date,
    SeatType seat_type, int count)
{
    std::string key = makeKey(train_id, date);
    auto& mtx = getMutex(key);
    std::unique_lock<std::shared_mutex> lock(mtx);  // 写锁

    auto& inv = getOrCreate(train_id, date);
    ensureSeatType(inv, seat_type, 0);  // 确保该席位已初始化

    auto& bitmap = inv.seat_maps[seat_type];

    // 找到 count 个未售座位
    std::vector<uint16_t> assigned;
    for (size_t i = 0; i < bitmap.seats.size() && static_cast<int>(assigned.size()) < count; ++i) {
        if (!bitmap.seats[i]) {
            bitmap.seats[i] = true;
            assigned.push_back(static_cast<uint16_t>(i + 1));  // 座位号 1-based
        }
    }

    if (static_cast<int>(assigned.size()) < count) {
        // 余票不足，回滚
        for (uint16_t seat_num : assigned) {
            bitmap.seats[seat_num - 1] = false;
        }
        Logger::instance().warn("Insufficient seats: " + train_id
            + " " + date + " " + std::to_string(count) + " needed, "
            + std::to_string(assigned.size()) + " available");
        return {{}, false};
    }


    Logger::instance().info("Reserved " + std::to_string(count) + " seats on " + train_id);

    Reservation result;
    result.seat_numbers = assigned;
    result.success = true;
    return result;
}

// ── 释放座位 ──

void SeatInventory::release(const std::string& train_id, const std::string& date,
                             SeatType seat_type, const std::vector<uint16_t>& seat_numbers) {
    std::string key = makeKey(train_id, date);
    auto& mtx = getMutex(key);
    std::unique_lock<std::shared_mutex> lock(mtx);

    auto& inv = getOrCreate(train_id, date);
    auto it = inv.seat_maps.find(seat_type);
    if (it == inv.seat_maps.end()) return;

    for (uint16_t seat_num : seat_numbers) {
        if (seat_num > 0 && seat_num <= static_cast<uint16_t>(it->second.seats.size())) {
            it->second.seats[seat_num - 1] = false;
        }
    }

    Logger::instance().info("Released " + std::to_string(seat_numbers.size())
        + " seats on " + train_id);
}

void SeatInventory::markSold(const std::string& train_id, const std::string& date,
                              SeatType seat_type, uint16_t seat_number) {
    if (seat_number == 0) return;
    std::string key = makeKey(train_id, date);
    auto& mtx = getMutex(key);
    std::unique_lock<std::shared_mutex> lock(mtx);

    auto& inv = getOrCreate(train_id, date);
    ensureSeatType(inv, seat_type, seat_number);

    auto& bitmap = inv.seat_maps[seat_type];
    if (seat_number <= bitmap.seats.size()) {
        bitmap.seats[seat_number - 1] = true;
    }
}

// ── 内部实现 ──

std::shared_mutex& SeatInventory::getMutex(const std::string& key) {
    {
        std::shared_lock<std::shared_mutex> lock(global_mutex_);
        auto it = mutexes_.find(key);
        if (it != mutexes_.end()) return *it->second;
    }
    {
        std::unique_lock<std::shared_mutex> lock(global_mutex_);
        auto it = mutexes_.find(key);
        if (it != mutexes_.end()) return *it->second;

        mutexes_[key] = std::make_unique<std::shared_mutex>();
        return *mutexes_[key];
    }
}

SeatInventory::TrainInventory& SeatInventory::getOrCreate(
    const std::string& train_id, const std::string& date)
{
    std::string key = makeKey(train_id, date);
    auto it = inventories_.find(key);
    if (it != inventories_.end()) return it->second;

    // 惰性初始化：从列车种子数据读取席位配置
    TrainInventory inv;
    auto* train = DataStore::instance().getTrain(train_id);
    if (train) {
        auto& cfg = train->seat_config;
        ensureSeatType(inv, SeatType::BUSINESS,     cfg.business_seats);
        ensureSeatType(inv, SeatType::FIRST,        cfg.first_seats);
        ensureSeatType(inv, SeatType::SECOND,       cfg.second_seats);
        ensureSeatType(inv, SeatType::HARD_SLEEPER, cfg.hard_sleeper);
        ensureSeatType(inv, SeatType::HARD_SEAT,    cfg.hard_seat);
        ensureSeatType(inv, SeatType::NO_SEAT,      cfg.no_seat);
    }

    auto [inserted_it, _] = inventories_.emplace(key, std::move(inv));
    return inserted_it->second;
}

void SeatInventory::ensureSeatType(TrainInventory& inv, SeatType type, uint16_t count) {
    if (count == 0) return;  // 该席位不存在
    if (inv.seat_maps.count(type)) return;  // 已初始化

    SeatBitmap bitmap;
    bitmap.seats.resize(count, false);  // 全部初始化为可售
    inv.seat_maps[type] = std::move(bitmap);
}
