// train_query.cpp — 列车查询实现
#include "train_query.h"
#include "data_store.h"
#include "railway_graph.h"
#include "seat_inventory.h"
#include "logger.h"

#include <algorithm>
#include <set>
#include <queue>

// ── 公开接口 ──

QueryResult TrainQuery::query(uint32_t from_station, uint32_t to_station,
                               const std::string& date) {
    QueryResult result;
    auto& ds = DataStore::instance();

    // ── 直达查询 ──
    for (const auto& train : ds.getAllTrains()) {
        if (train.status != TrainStatus::ACTIVE) continue;

        auto [from_idx, to_idx] = findStops(train, from_station, to_station);
        if (from_idx < 0 || to_idx < 0) continue;  // 该车次不覆盖此 OD
        if (from_idx >= to_idx) continue;           // 方向错误

        QueryResultItem item;
        item.train_id = train.id;
        item.train_type = train.type;
        item.from_station = from_station;
        item.to_station = to_station;
        item.departure_time = train.stops[from_idx].departure;
        item.arrival_time = train.stops[to_idx].arrival;
        item.duration_minutes = timeDiff(item.departure_time, item.arrival_time);
        item.stops = train.stops;

        // 票价计算：使用 RailwayGraph 获取最短路径里程
        RailwayGraph graph;
        graph.build(ds.getAllLines());
        auto path = graph.shortestPath(from_station, to_station);
        item.price = calcPrice(path.total_distance_km, SeatType::SECOND);

        // 查可用座位（仅查数量，不锁定）
        item.available_seats = getAvailableSeats(train.id, date);

        result.direct.push_back(item);
    }

    // 按时长排序
    std::sort(result.direct.begin(), result.direct.end(),
        [](const QueryResultItem& a, const QueryResultItem& b) {
            return a.duration_minutes < b.duration_minutes;
        });

    // ── 换乘查询 ──
    // 找中转站：from 和 to 的共同邻居（在铁路网图中）
    auto transfers = findTransferStations(from_station, to_station);
    for (uint32_t transfer_id : transfers) {
        // 找第一段：from → transfer 的列车
        for (const auto& t1 : ds.getAllTrains()) {
            if (t1.status != TrainStatus::ACTIVE) continue;
            auto [f1, t1_idx] = findStops(t1, from_station, transfer_id);
            if (f1 < 0 || t1_idx < 0 || f1 >= t1_idx) continue;
            int arrival_at_transfer = t1.stops[t1_idx].arrival;

            // 找第二段：transfer → to 的列车
            for (const auto& t2 : ds.getAllTrains()) {
                if (t2.status != TrainStatus::ACTIVE) continue;
                if (t1.id == t2.id) continue;  // 同车次不算换乘
                auto [f2, to_idx2] = findStops(t2, transfer_id, to_station);
                if (f2 < 0 || to_idx2 < 0 || f2 >= to_idx2) continue;
                int departure_from_transfer = t2.stops[f2].departure;

                // 换乘时间窗口 ≥ 30 分钟
                int gap = timeDiff(arrival_at_transfer, departure_from_transfer);
                if (gap < 30) continue;

                QueryResultItem item;
                item.train_id = t1.id + " → " + t2.id;
                item.train_type = t1.type;
                item.from_station = from_station;
                item.to_station = to_station;
                item.departure_time = t1.stops[f1].departure;
                item.arrival_time = t2.stops[to_idx2].arrival;
                item.duration_minutes = timeDiff(item.departure_time, item.arrival_time);
                item.stops = t1.stops;  // 只展示第一段停站（实际可同时返回两段）
                item.is_transfer = true;
                auto* station = ds.getStation(transfer_id);
                item.transfer_station = station ? station->name : "unknown";
                item.second_train_id = t2.id;

                // 里程累加
                RailwayGraph graph;
                graph.build(ds.getAllLines());
                auto p1 = graph.shortestPath(from_station, transfer_id);
                auto p2 = graph.shortestPath(transfer_id, to_station);
                item.price = calcPrice(p1.total_distance_km + p2.total_distance_km,
                                       SeatType::SECOND);

                result.transfers.push_back(item);
            }
        }
    }

    // 换乘也按时长排序
    std::sort(result.transfers.begin(), result.transfers.end(),
        [](const QueryResultItem& a, const QueryResultItem& b) {
            return a.duration_minutes < b.duration_minutes;
        });

    return result;
}

// ── 内部实现 ──

std::pair<int, int> TrainQuery::findStops(const Train& train, uint32_t from, uint32_t to) {
    int from_idx = -1, to_idx = -1;
    for (size_t i = 0; i < train.stops.size(); ++i) {
        if (train.stops[i].station_id == from) from_idx = static_cast<int>(i);
        if (train.stops[i].station_id == to) to_idx = static_cast<int>(i);
    }
    return {from_idx, to_idx};
}

int TrainQuery::timeDiff(int from_hhmm, int to_hhmm) {
    if (from_hhmm < 0 || to_hhmm < 0) return 9999;
    int from_min = (from_hhmm / 100) * 60 + (from_hhmm % 100);
    int to_min = (to_hhmm / 100) * 60 + (to_hhmm % 100);
    if (to_min < from_min) to_min += 24 * 60;  // 跨天
    return to_min - from_min;
}

double TrainQuery::calcPrice(double distance_km, SeatType seat_type) {
    // 每公里基准费率（元/km），按席位乘以不同倍率
    const double BASE_RATE = 0.30;  // 二等座基准费率（实际约 0.3-0.4 元/km）
    double multiplier = 1.0;
    switch (seat_type) {
        case SeatType::BUSINESS:     multiplier = 3.0; break;
        case SeatType::FIRST:        multiplier = 2.0; break;
        case SeatType::SECOND:       multiplier = 1.0; break;
        case SeatType::HARD_SLEEPER: multiplier = 0.8; break;
        case SeatType::HARD_SEAT:    multiplier = 0.4; break;
        case SeatType::NO_SEAT:      multiplier = 0.3; break;
    }
    return distance_km * BASE_RATE * multiplier;
}

std::vector<uint32_t> TrainQuery::findTransferStations(uint32_t from, uint32_t to) {
    auto& ds = DataStore::instance();
    RailwayGraph graph;
    graph.build(ds.getAllLines());

    // 找 from 的所有邻居 → 筛选也能到达 to 的
    const auto& adj = graph.getAdjacency();
    auto from_it = adj.find(from);
    if (from_it == adj.end()) return {};

    std::vector<uint32_t> result;
    std::set<uint32_t> seen;

    for (const auto& [neighbor, _] : from_it->second) {
        if (neighbor == to || seen.count(neighbor)) continue;
        seen.insert(neighbor);

        // 检查 neighbor 是否能到达 to（直接相邻或通过路径）
        auto to_it = adj.find(neighbor);
        if (to_it == adj.end()) continue;
        for (const auto& [n2, _] : to_it->second) {
            if (n2 == to) {
                result.push_back(neighbor);
                break;
            }
        }
    }

    // BFS 深度 2（找需要两次换乘的中转站）
    for (const auto& [n1, _] : from_it->second) {
        auto n1_it = adj.find(n1);
        if (n1_it == adj.end()) continue;
        for (const auto& [n2, _] : n1_it->second) {
            if (n2 == to || seen.count(n2)) continue;
            seen.insert(n2);
            auto n2_it = adj.find(n2);
            if (n2_it == adj.end()) continue;
            for (const auto& [n3, _] : n2_it->second) {
                if (n3 == to) {
                    result.push_back(n2);
                    break;
                }
            }
        }
    }

    return result;
}

SeatConfig TrainQuery::getAvailableSeats(const std::string& train_id, const std::string& date) {
    return SeatInventory::instance().getAvailable(train_id, date);
}
