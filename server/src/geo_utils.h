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
