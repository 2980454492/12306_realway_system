// train_query.h — 列车余票查询服务，支持直达和一次换乘
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <cstdint>

/** 单趟查询结果 */
struct QueryResultItem {
    std::string train_id;        // 车次号
    TrainType train_type;        // 图定/临客
    uint32_t from_station;       // 出发站 ID
    uint32_t to_station;         // 到达站 ID
    int departure_time;          // 出发时间 HHMM
    int arrival_time;            // 到达时间 HHMM
    int duration_minutes;        // 历时（分钟）
    std::vector<Stop> stops;     // 该列车完整停站序列
    SeatConfig available_seats;  // 该日期的余票数量（从库存查询）
    double price;                // 票价（按里程×席位倍率估算，取二等座为基准）
    double distance_km = 0.0;    // 走行里程（km），沿列车 route_stations 逐段 Haversine 累加
    bool is_transfer = false;    // 是否为换乘方案
    std::string transfer_station;   // 换乘站名
    std::string second_train_id;    // 第二段车次
    std::vector<Stop> second_stops; // 第二段列车停站
    // 换乘详情（前端展示用）
    int transfer_arrival_time = 0;     // T1 到达中转站时间 HHMM
    int transfer_departure_time = 0;   // T2 从中转站出发时间 HHMM
    int transfer_gap_minutes = 0;      // 换乘间隔（分钟）
    // 每程独立余票和票价（换乘时前端分两行展示）
    SeatConfig first_leg_seats;       // 第一程余票
    SeatConfig second_leg_seats;      // 第二程余票
    double first_leg_price = 0.0;     // 第一程票价（二等座基准）
    double second_leg_price = 0.0;    // 第二程票价（二等座基准）
};

/** 车站查询单条结果 */
struct StationQueryItem {
    std::string train_id;
    TrainType train_type;
    std::string from_station_name;   // 始发站名
    std::string to_station_name;     // 终到站名
    int arrival_time = 0;            // 到达查询站的时刻 HHMM，始发站为 -1
    int departure_time = 0;          // 从查询站出发的时刻 HHMM，终到站为 -1
    std::vector<Stop> stops;         // 完整停站序列
};

/** 一次查询的完整结果 */
struct QueryResult {
    std::vector<QueryResultItem> direct;     // 直达结果
    std::vector<QueryResultItem> transfers;  // 换乘结果
};

/**
 * TrainQuery — 列车余票查询逻辑。
 * 直达：基于车站-列车索引 O(1) 查找，匹配停站序列。
 * 换乘：遍历 from 出发列车的后续停站作为候选中转站，通过索引查找中转→to 的列车。
 *       地理约束中转站在起止站之间，换乘时间窗口 ≥ 30 分钟。
 * 内部工具函数全部在 .cpp 匿名 namespace 中，不暴露。
 */
class TrainQuery {
public:
    /**
     * 查询 from → to 的可用列车。
     * @param date 乘车日期（yyyy-MM-dd），用于查询座位库存
     * @return 直达和换乘两种方案，前端负责排序和筛选
     */
    static QueryResult query(uint32_t from_station, uint32_t to_station,
                             const std::string& date);

    /** 查询经停某站的所有列车，结果按发车时间升序（终到站按到达时间） */
    static std::vector<StationQueryItem> queryByStation(uint32_t station_id);
};
