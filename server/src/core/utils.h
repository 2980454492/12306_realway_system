// utils.h — 全局工具函数（UUID、时间、地理、序列查找、路线计算）
#pragma once

#include "models.h"
#include "data/data_store.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <tuple>
#include <cmath>

// ── UUID ──

/** 生成 UUID v4 字符串 */
inline std::string generateUuid() {
    static std::mt19937_64 rng(std::random_device{}());
    static std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(rng), b = dist(rng);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << ((a >> 32) & 0xFFFFFFFF)
        << "-" << std::setw(4) << ((a >> 16) & 0xFFFF)
        << "-4" << std::setw(3) << (a & 0xFFF)
        << "-8" << std::setw(3) << ((b >> 48) & 0xFFF)
        << "-" << std::setw(4) << ((b >> 32) & 0xFFFF)
        << std::setw(8) << (b & 0xFFFFFFFF);
    return oss.str();
}

// ── 时间工具 ──

/** 当前本地时间 tm 结构（所有时间函数的唯一 system_clock 调用点） */
inline std::tm nowTm() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

/** 今天的日期字符串 yyyy-MM-dd */
inline std::string todayStr() {
    auto tm = nowTm();
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

/** 当前 HHMM（如 1430 = 14:30） */
inline int nowHHMM() {
    auto tm = nowTm();
    return tm.tm_hour * 100 + tm.tm_min;
}

/** 当前 UTC 时间 ISO 8601 字符串（用于订单时间戳） */
inline std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&t, &utc);
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/** 检查日期字符串（yyyy-MM-dd）是否为今天 */
inline bool isToday(const std::string& date) {
    return date == todayStr();
}

/**
 * 检查车票是否未过期：日期在 [today, today+maxDays]，且若为今天则发车时间未过。
 * @param departure_hhmm  发车 HHMM，为 0 时仅做日期比较
 */
inline bool isFuture(const std::string& date, int maxDays, int departure_hhmm = 0) {
    auto today = todayStr();
    if (date < today) return false;

    auto tm = nowTm();
    auto max_t = std::mktime(&tm) + maxDays * 86400;
    std::tm max_tm{};
    localtime_r(&max_t, &max_tm);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &max_tm);
    if (date > std::string(buf)) return false;

    // 今天的票，进一步检查发车时间是否已过
    if (departure_hhmm > 0 && date == today) {
        return nowHHMM() < departure_hhmm;
    }
    return true;
}

/** 计算两个 HHMM 时间差（分钟），支持跨天（如 2300→0100 = 120min） */
inline int timeDiff(int from_hhmm, int to_hhmm) {
    if (from_hhmm < 0 || to_hhmm < 0) return 9999;
    int from_min = (from_hhmm / 100) * 60 + (from_hhmm % 100);
    int to_min   = (to_hhmm / 100) * 60 + (to_hhmm % 100);
    if (to_min < from_min) to_min += 24 * 60;
    return to_min - from_min;
}

// ── 序列查找 ──

/** 在车站 ID 序列中查找 from 和 to 的索引。to 必须在 from 之后出现。 */
inline std::pair<int, int> findIndices(const std::vector<uint32_t>& ids, uint32_t from, uint32_t to) {
    int from_idx = -1, to_idx = -1;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == from) from_idx = static_cast<int>(i);
        if (ids[i] == to && from_idx >= 0) {
            to_idx = static_cast<int>(i);
            break;
        }
    }
    return {from_idx, to_idx};
}

// ── 地理工具 ──

/** Haversine 公式：计算两经纬度间的大圆距离（km） */
inline double haversineDist(const Station& a, const Station& b) {
    const double EARTH_RADIUS_KM = 6371.0;
    double lat1 = a.latitude * M_PI / 180.0;
    double lat2 = b.latitude * M_PI / 180.0;
    double dlat = lat2 - lat1;
    double dlon = (b.longitude - a.longitude) * M_PI / 180.0;

    double sin_dlat = std::sin(dlat / 2.0);
    double sin_dlon = std::sin(dlon / 2.0);
    double h = sin_dlat * sin_dlat + std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
    return 2.0 * EARTH_RADIUS_KM * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

/** 席位票价倍率（相对于二等座） */
inline constexpr double seatPriceMultiplier(SeatType type) {
    switch (type) {
        case SeatType::BUSINESS:     return 3.0;
        case SeatType::FIRST:        return 2.0;
        case SeatType::SECOND:       return 1.0;
        case SeatType::HARD_SLEEPER: return 0.8;
        case SeatType::HARD_SEAT:    return 0.4;
        case SeatType::NO_SEAT:      return 0.3;
    }
    return 1.0;
}

/** 二等座基准费率（元/km） */
inline constexpr double BASE_RATE_PER_KM = 0.30;

/** 购票/查票最大提前天数（12306 为 15 天，含今天） */
inline constexpr int MAX_ADVANCE_DAYS = 14;

// ── 路线计算 ──

/**
 * 计算列车从 from_station 到 to_station 的实际走行里程。
 * 使用 route_stations（含所有经过站）逐段累加 Haversine。
 */
inline double calcRouteDistance(const Train& train, uint32_t from_station, uint32_t to_station,
                                DataStore& ds) {
    int from_idx = -1, to_idx = -1;
    std::vector<uint32_t> fallback_ids;
    const std::vector<uint32_t>* ids = &train.route_stations;
    if (ids->empty()) {
        for (const auto& s : train.stops) fallback_ids.push_back(s.station_id);
        ids = &fallback_ids;
    }

    std::tie(from_idx, to_idx) = findIndices(*ids, from_station, to_station);
    if (from_idx < 0 || to_idx < 0 || from_idx >= to_idx) return 0.0;

    double total = 0.0;
    for (int i = from_idx; i < to_idx; ++i) {
        auto* sa = ds.getStation((*ids)[i]);
        auto* sb = ds.getStation((*ids)[i + 1]);
        if (sa && sb) total += haversineDist(*sa, *sb);
    }
    return total;
}
