#!/usr/bin/env python3
"""刷新 stations.json 坐标；lines.json 存城市名，C++ 加载时自动转换为 ID"""
import json, os
from station_data import *


def refresh_coords(stations):
    """用 CITY_COORDS 刷新坐标，不改变站点列表"""
    updated = 0
    for s in stations:
        if s["city"] in CITY_COORDS:
            lat, lng = CITY_COORDS[s["city"]]
        elif s["name"] in CITY_COORDS:
            lat, lng = CITY_COORDS[s["name"]]
        else:
            continue
        for d, dl, dg in DIRECTIONS:
            if s["name"].endswith(d):
                lat += dl; lng += dg
        s["latitude"] = round(lat, 4)
        s["longitude"] = round(lng, 4)
        updated += 1
    return updated


if __name__ == "__main__":
    spath = os.path.join(CONFIG_DIR, "stations.json")
    lpath = os.path.join(CONFIG_DIR, "lines.json")

    with open(spath, "r", encoding="utf-8") as f:
        stations = json.load(f)
    print(f"站点 {len(stations)} 个")

    updated = refresh_coords(stations)
    print(f"已更新 {updated} 个坐标")

    with open(spath, "w", encoding="utf-8") as f:
        json.dump(stations, f, ensure_ascii=False, indent=2)
    print("已保存 stations.json")
    print(f"lines.json 使用城市名格式，无需更新")
