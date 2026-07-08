// geo_utils.h — 地理工具函数（Haversine 距离计算）
#pragma once

#include "models.h"
#include <cmath>

/** Haversine 公式：计算两经纬度间的大圆距离（km） */
inline double haversineDist(const Station& a, const Station& b) {
    const double EARTH_RADIUS_KM = 6371.0;  // 地球平均半径（km）
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
inline double seatPriceMultiplier(SeatType type) {
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

#include <chrono>
#include <ctime>
#include <string>

/** 当前 HHMM（如 1430 = 14:30），用于判断是否已发车 */
inline int nowHHMM() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm.tm_hour * 100 + tm.tm_min;
}

/** 检查给定日期字符串（yyyy-MM-dd）是否为今天 */
inline bool isToday(const std::string& date) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return date == std::string(buf);
}

/** 检查日期是否在 [今天, 今天+maxDays] 范围内 */
inline bool isTodayOrFuture(const std::string& date, int maxDays) {
    char buf[11];
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    std::string today(buf);

    auto min_t = std::mktime(&tm);
    auto max_t = min_t + maxDays * 86400;
    std::tm max_tm{};
    localtime_r(&max_t, &max_tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &max_tm);
    std::string maxDay(buf);

    return date >= today && date <= maxDay;
}
