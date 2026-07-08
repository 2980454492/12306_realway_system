// geo_utils.h — 地理工具函数（Haversine 距离计算）
#pragma once

#include "models.h"
#include <cmath>

/** Haversine 公式：计算两经纬度间的大圆距离（km） */
inline double haversineDist(const Station& a, const Station& b) {
    const double R = 6371.0;  // 地球平均半径（km）
    double lat1 = a.latitude * M_PI / 180.0;
    double lat2 = b.latitude * M_PI / 180.0;
    double dlat = lat2 - lat1;
    double dlon = (b.longitude - a.longitude) * M_PI / 180.0;

    double sin_dlat = std::sin(dlat / 2.0);
    double sin_dlon = std::sin(dlon / 2.0);
    double h = sin_dlat * sin_dlat + std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
    return 2.0 * R * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}
