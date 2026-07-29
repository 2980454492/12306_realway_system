#!/usr/bin/env python3
"""从 12306 筛选主要车站 + 生成全国线路"""
import json, os
from station_data import *

def select_major(all_stations):
    """每个城市只保留一个代表性车站"""
    selected, seen = [], set()
    for s in all_stations:
        city = s["city"]
        if city in seen or city in SKIP_CITIES:
            continue
        seen.add(city)
        selected.append(s)
    # 按城市名排序，选出最佳代表站
    city_best = {}
    for s in selected:
        c = s["city"]; n = s["name"]
        if c not in city_best:
            city_best[c] = s
        else:
            # 优先保留不带方向的站名
            old = city_best[c]["name"]
            if n == c or n == c + "站":
                city_best[c] = s
            elif old != c and old != c + "站" and (n == c or n == c + "站"):
                city_best[c] = s
    return sorted(city_best.values(), key=lambda x: x["name"])


def gen_lines(stations):
    """基于 stations 数据生成全国主要铁路线路"""
    city_ids = {}
    for s in stations:
        city_ids.setdefault(s["city"], []).append(s["id"])

    lines_data = [
        ("京包高铁",["乌兰察布","呼和浩特","包头"],320,300,"HIGH_SPEED"),
        ("呼鄂城际",["呼和浩特","鄂尔多斯"],210,200,"INTERCITY"),
        ("包兰铁路",["包头","巴彦淖尔","乌海"],405,120,"NORMAL"),
        ("京沪高铁",["北京","天津","济南","徐州","南京","上海"],1318,350,"HIGH_SPEED"),
        ("京广高铁",["北京","保定","石家庄","邯郸","安阳","郑州","武汉","长沙","广州"],2298,350,"HIGH_SPEED"),
        ("京哈高铁",["北京","沈阳","长春","哈尔滨"],1260,350,"HIGH_SPEED"),
        ("哈大高铁",["哈尔滨","长春","沈阳","大连"],920,300,"HIGH_SPEED"),
        ("京张高铁",["北京","张家口","乌兰察布"],350,350,"HIGH_SPEED"),
        ("郑西高铁",["郑州","洛阳","西安"],510,300,"HIGH_SPEED"),
        ("西成高铁",["西安","成都"],658,250,"HIGH_SPEED"),
        ("沪杭高铁",["上海","杭州"],169,350,"HIGH_SPEED"),
        ("沪昆高铁",["上海","杭州","南昌","长沙","贵阳","昆明"],2266,300,"HIGH_SPEED"),
        ("广深港高铁",["广州","深圳"],102,350,"HIGH_SPEED"),
        ("成渝高铁",["成都","重庆"],308,300,"HIGH_SPEED"),
        ("银西高铁",["银川","西安"],618,250,"HIGH_SPEED"),
        ("兰新高铁",["兰州","西宁"],200,250,"HIGH_SPEED"),
        ("大西高铁",["大同","太原"],290,300,"HIGH_SPEED"),
        ("济青高铁",["济南","青岛"],320,300,"HIGH_SPEED"),
        ("合宁高铁",["合肥","南京"],156,300,"HIGH_SPEED"),
        ("福厦高铁",["福州","厦门"],280,350,"HIGH_SPEED"),
        ("杭甬高铁",["杭州","宁波"],150,350,"HIGH_SPEED"),
        ("京广铁路",["北京","保定","石家庄","邯郸","郑州","武汉","长沙","广州"],2320,160,"NORMAL"),
        ("陇海铁路",["徐州","商丘","郑州","洛阳","西安","宝鸡","兰州"],1759,160,"NORMAL"),
        ("呼太铁路",["呼和浩特","太原"],510,160,"NORMAL"),
        ("南昆铁路",["南宁","昆明"],828,120,"NORMAL"),
        ("湘桂铁路",["长沙","桂林","柳州","南宁"],1013,120,"NORMAL"),
        ("沪蓉高铁",["上海","南京","合肥","武汉","重庆","成都"],2078,300,"HIGH_SPEED"),
        ("徐兰高铁",["徐州","郑州","西安","兰州"],1400,300,"HIGH_SPEED"),
        ("广昆高铁",["广州","南宁","昆明"],1280,300,"HIGH_SPEED"),
        ("南疆铁路",["乌鲁木齐","吐鲁番","库尔勒","阿克苏","喀什"],1500,120,"NORMAL"),
    ]
    result = []
    for lid, (name, cities, km, speed, ltype) in enumerate(lines_data, 1):
        ids = [city_ids[c][0] for c in cities if c in city_ids and city_ids[c]]
        if len(ids) >= 2:
            result.append({"id": lid, "name": name, "stations": ids,
                           "distance_km": km, "max_speed_kmh": speed, "type": ltype})
    return result


if __name__ == "__main__":
    raw = fetch_raw()
    print(f"共解析 {len(raw)} 个车站")
    # 过滤非客运站 + 每城一站
    filtered = [s for s in raw if not any(kw in s["name"] for kw in NON_PASSENGER_KW)]
    major = select_major(filtered)
    print(f"筛选后 {len(major)} 个主要车站")

    # 生成坐标
    stations_with_coords = gen_coords([{"name": s["name"], "city": s["city"], "code": s["code"], "bureau": s["bureau"]} for s in major])
    # 保留 city 字段并重编号
    for i, s in enumerate(stations_with_coords):
        s["id"] = i + 1
        s["city"] = major[i]["city"]

    path = os.path.join(CONFIG_DIR, "stations.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(stations_with_coords, f, ensure_ascii=False, indent=2)
    print(f"已写入 {path} ({len(stations_with_coords)} 个站点)")

    # 生成线路
    lines = gen_lines(stations_with_coords)
    lpath = os.path.join(CONFIG_DIR, "lines.json")
    with open(lpath, "w", encoding="utf-8") as f:
        json.dump(lines, f, ensure_ascii=False, indent=2)
    print(f"已写入 {lpath} ({len(lines)} 条线路)")
