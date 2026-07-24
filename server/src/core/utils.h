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

/** 生成 UUID v4 随机字符串（用于订单号等唯一标识） */
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

/** 当前本地时间，所有时间函数的唯一 system_clock 入口 */
inline std::tm nowTm() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

/** 今天的日期，格式 yyyy-MM-dd */
inline std::string todayStr() {
    auto tm = nowTm();
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

/** 当前时刻 HHMM 整数（如 1430） */
inline int nowHHMM() {
    auto tm = nowTm();
    return tm.tm_hour * 100 + tm.tm_min;
}

/** 当前本地时间，ISO 8601 格式（订单/审批时间戳用） */
inline std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&t, &local);
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

/** date 是否为今天 */
inline bool isToday(const std::string& date) {
    return date == todayStr();
}

/** 车票是否未过期：日期在 [today, today+maxDays]，departure_hhmm>0 时校验发车时间未过 */
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

/** 两个 HHMM 的时间差（分钟），跨天自动加 24h */
inline int timeDiff(int from_hhmm, int to_hhmm) {
    if (from_hhmm < 0 || to_hhmm < 0) return 9999;
    int from_min = (from_hhmm / 100) * 60 + (from_hhmm % 100);
    int to_min   = (to_hhmm / 100) * 60 + (to_hhmm % 100);
    if (to_min < from_min) to_min += 24 * 60;
    return to_min - from_min;
}

// ── 序列查找 ──

/** 在 ID 序列中查找 from/to 的索引，to 须在 from 之后 */
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

/** 两站点间的大圆距离（km），Haversine 公式 */
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

/** 席位票价倍率，以二等座为 1.0 */
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

/** 二等座每公里基准费率（元） */
inline constexpr double BASE_RATE_PER_KM = 0.30;

/** 购票/查票最大提前天数（12306 为 15 天即 today+14） */
inline constexpr int MAX_ADVANCE_DAYS = 14;

/** 新增列车最少提前天数 */
inline constexpr int MIN_NEW_TRAIN_DAYS = 3;

// ── 路线计算 ──

/** 从 stops 推导 segments（不存于 JSON，每次按需构建） */
inline std::vector<RouteSegment> buildSegments(const Train& train, const DataStore& ds) {
    std::vector<RouteSegment> segs;
    for (size_t i = 0; i + 1 < train.stops.size(); ++i) {
        RouteSegment seg;
        seg.from_station = train.stops[i].station_id;
        seg.to_station   = train.stops[i + 1].station_id;
        seg.enter_time   = train.stops[i].departure;
        seg.leave_time   = train.stops[i + 1].arrival;
        seg.line_id      = train.stops[i + 1].line_id;
        auto* a = ds.getStation(seg.from_station);
        auto* b = ds.getStation(seg.to_station);
        if (a && b) {
            seg.distance_km = haversineDist(*a, *b);
            if (seg.enter_time > 0 && seg.leave_time > 0 && seg.distance_km > 0) {
                int em = (seg.enter_time / 100) * 60 + (seg.enter_time % 100);
                int lm = (seg.leave_time / 100) * 60 + (seg.leave_time % 100);
                if (lm > em)
                    seg.speed_kmh = static_cast<int>(seg.distance_km / ((lm - em) / 60.0));
            }
        }
        segs.push_back(seg);
    }
    return segs;
}

/** 计算列车从 from 到 to 的实际走行里程，沿 stops 逐段累加 Haversine */
inline double calcRouteDistance(const Train& train, uint32_t from_station, uint32_t to_station,
                                DataStore& ds) {
    int from_idx = -1, to_idx = -1;
    std::vector<uint32_t> ids;
    for (const auto& s : train.stops)
        ids.push_back(s.station_id);

    std::tie(from_idx, to_idx) = findIndices(ids, from_station, to_station);
    if (from_idx < 0 || to_idx < 0 || from_idx >= to_idx) return 0.0;

    double total = 0.0;
    for (int i = from_idx; i < to_idx; ++i) {
        auto* sa = ds.getStation(ids[i]);
        auto* sb = ds.getStation(ids[i + 1]);
        if (sa && sb) total += haversineDist(*sa, *sb);
    }
    return total;
}
