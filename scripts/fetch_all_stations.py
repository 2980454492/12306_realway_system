#!/usr/bin/env python3
"""从 12306 抓取全部 3374+ 车站，生成 coordinates stations.json"""
import json, os
from station_data import *

if __name__ == "__main__":
    raw = fetch_raw()
    print(f"共解析 {len(raw)} 个客运站")
    # 过滤非客运站
    filtered = [s for s in raw if not any(kw in s["name"] for kw in NON_PASSENGER_KW)]
    print(f"过滤后 {len(filtered)} 个")
    stations = gen_coords(filtered)
    path = os.path.join(CONFIG_DIR, "stations.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(stations, f, ensure_ascii=False, indent=2)
    print(f"已写入 {path} ({len(stations)} 个站点)")
