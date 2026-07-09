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
    std::string transfer_station; // 换乘站名（仅换乘方案）
    std::string second_train_id;  // 第二段车次（仅换乘方案）
    std::vector<Stop> second_stops; // 第二段列车停站（仅换乘方案）
};

/** 一次查询的完整结果 */
struct QueryResult {
    std::vector<QueryResultItem> direct;     // 直达结果
    std::vector<QueryResultItem> transfers;  // 换乘结果
};

/**
 * TrainQuery — 列车余票查询逻辑。
 * 直达：遍历所有列车，匹配停站序列。
 * 换乘：铁路网 BFS 找中转站，验证两段车次时间衔接。
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
};
