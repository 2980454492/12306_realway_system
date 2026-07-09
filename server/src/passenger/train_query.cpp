// train_query.cpp — 列车余票查询实现
#include "passenger/train_query.h"
#include "data/data_store.h"
#include "data/railway_graph.h"
#include "passenger/seat_inventory.h"
#include "geo_utils.h"
#include "core/logger.h"

#include <algorithm>
#include <set>
#include <queue>

namespace {

/**
 * 计算列车从 from_station 到 to_station 的实际走行里程。
 * 使用 route_stations（含所有经过站，不只是停站）逐段累加 Haversine。
 * 铁路列车沿轨道依次经过各中间站，不是直线飞行——必须逐段求和。
 */
double calcRouteDistance(const Train& train, uint32_t from_station, uint32_t to_station,
                         DataStore& ds) {
    int from_idx = -1, to_idx = -1;
    // 优先用 route_stations（更精确），fallback 用 stops 的 station_id
    std::vector<uint32_t> fallback_ids;
    const std::vector<uint32_t>* ids = &train.route_stations;
    if (ids->empty()) {
        for (const auto& s : train.stops) fallback_ids.push_back(s.station_id);
        ids = &fallback_ids;
    }

    for (size_t i = 0; i < ids->size(); ++i) {
        if ((*ids)[i] == from_station) from_idx = static_cast<int>(i);
        if ((*ids)[i] == to_station && from_idx >= 0) {
            to_idx = static_cast<int>(i);
            break;
        }
    }
    if (from_idx < 0 || to_idx < 0 || from_idx >= to_idx) return 0.0;

    double total = 0.0;
    for (int i = from_idx; i < to_idx; ++i) {
        auto* sa = ds.getStation((*ids)[i]);
        auto* sb = ds.getStation((*ids)[i + 1]);
        if (sa && sb) {
            total += haversineDist(*sa, *sb);
        }
    }
    return total;
}

}  // namespace

// ── 公开接口 ──

QueryResult TrainQuery::query(uint32_t from_station, uint32_t to_station,
                               const std::string& date) {
    QueryResult result;
    auto& ds = DataStore::instance();

    // 预构建铁路网图（仅用于换乘站查找，不用于票价里程）
    RailwayGraph graph;
    graph.build(ds.getAllLines());

    // ── 直达查询 ──
    for (const auto& train : ds.getAllTrains()) {
        if (train.status != TrainStatus::ACTIVE) continue;

        auto [from_idx, to_idx] = findStops(train, from_station, to_station);
        if (from_idx < 0 || to_idx < 0) continue;  // 该车次不覆盖此 OD
        if (from_idx >= to_idx) continue;           // 方向错误

        // 查当天车次时，过滤已发车的
        int dep_hhmm = train.stops[from_idx].departure;
        if (dep_hhmm > 0 && isToday(date) && nowHHMM() > dep_hhmm) continue;

        QueryResultItem item;
        item.train_id = train.id;
        item.train_type = train.type;
        item.from_station = from_station;
        item.to_station = to_station;
        item.departure_time = train.stops[from_idx].departure;
        item.arrival_time = train.stops[to_idx].arrival;
        item.duration_minutes = timeDiff(item.departure_time, item.arrival_time);
        item.stops = train.stops;

        // 票价里程 = 沿该列车停站序列逐段累加（不是直线距离）
        double trip_km = calcRouteDistance(train, from_station, to_station, ds);
        item.distance_km = trip_km;
        item.price = calcPrice(trip_km, SeatType::SECOND);

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
    // 找中转站：from 和 to 的共同邻居（复用已构建的图）
    auto transfers = findTransferStations(from_station, to_station, graph);
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
                item.stops = t1.stops;
                item.is_transfer = true;
                auto* station = ds.getStation(transfer_id);
                item.transfer_station = station ? station->name : "unknown";
                item.second_train_id = t2.id;
                item.second_stops = t2.stops;

                // 换乘里程 = 两段列车各自的逐段累加之和
                double km1 = calcRouteDistance(t1, from_station, transfer_id, ds);
                double km2 = calcRouteDistance(t2, transfer_id, to_station, ds);
                item.distance_km = km1 + km2;
                item.price = calcPrice(km1 + km2, SeatType::SECOND);

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
    const double BASE_RATE = 0.30;  // 二等座基准费率（元/km）
    return distance_km * BASE_RATE * seatPriceMultiplier(seat_type);
}

std::vector<uint32_t> TrainQuery::findTransferStations(uint32_t from, uint32_t to,
                                                       const RailwayGraph& graph) {
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
