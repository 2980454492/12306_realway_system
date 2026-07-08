// railway_graph.cpp — RailwayGraph 实现
#include "railway_graph.h"

#include <queue>
#include <algorithm>
#include <map>
#include <cmath>
#include <unordered_map>

namespace {

/** Haversine 公式：计算两经纬度间的大圆距离（km） */
double haversineDist(const Station& a, const Station& b) {
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

}  // namespace

// ── 构建 ──

void RailwayGraph::build(const std::vector<Line>& lines, const std::vector<Station>& stations) {
    adjacency_.clear();
    invalidateCache();

    // 站点 ID → Station 指针，快速查找经纬度
    std::unordered_map<uint32_t, const Station*> station_map;
    for (const auto& s : stations) {
        station_map[s.id] = &s;
    }

    for (const auto& line : lines) {
        if (line.stations.size() < 2) continue;

        // 按线路沿途站点序列拆分为多段，Haversine 计算邻站实际地理距离
        for (size_t i = 0; i + 1 < line.stations.size(); ++i) {
            uint32_t id_a = line.stations[i];
            uint32_t id_b = line.stations[i + 1];

            auto sa = station_map.find(id_a);
            auto sb = station_map.find(id_b);
            if (sa == station_map.end() || sb == station_map.end()) continue;

            double dist = haversineDist(*sa->second, *sb->second);

            adjacency_[id_a].push_back({id_b, dist});
            adjacency_[id_b].push_back({id_a, dist});
        }
    }
}

// ── Dijkstra 最短路径 ──
// 按里程累计，返回路径最小的走法

PathResult RailwayGraph::shortestPath(uint32_t from, uint32_t to) const {
    // 查缓存
    if (cache_valid_) {
        auto cache_key = std::make_pair(from, to);
        auto it = shortest_cache_.find(cache_key);
        if (it != shortest_cache_.end()) {
            return it->second;
        }
    }

    PathResult result;

    // 起止站必须存在于图中
    if (adjacency_.find(from) == adjacency_.end() ||
        adjacency_.find(to) == adjacency_.end()) {
        result.found = false;
        return result;
    }

    // 使用优先队列的 Dijkstra（按里程累计）
    using QueueItem = std::pair<double, uint32_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pq;

    std::map<uint32_t, double> distance;
    std::map<uint32_t, uint32_t> previous;

    // 初始化所有节点距离为无穷大
    for (const auto& [station_id, _] : adjacency_) {
        distance[station_id] = std::numeric_limits<double>::infinity();
    }
    distance[from] = 0.0;
    pq.push({0.0, from});

    while (!pq.empty()) {
        auto [dist, current] = pq.top();
        pq.pop();

        // 跳过已找到的更短路径
        if (dist > distance[current]) continue;

        if (current == to) break;  // 已找到

        auto adj_it = adjacency_.find(current);
        if (adj_it == adjacency_.end()) continue;

        for (const auto& [neighbor, edge_dist] : adj_it->second) {
            double new_dist = dist + edge_dist;
            // 用 at() 而非 []——neighbor 一定在 distance 中（初始化阶段已遍历所有节点）
            if (new_dist < distance.at(neighbor)) {
                distance[neighbor] = new_dist;
                previous[neighbor] = current;
                pq.push({new_dist, neighbor});
            }
        }
    }

    // 用 find() 而非 []——避免 key 不存在时自动插入 0，掩盖"找不到路径"的错误
    auto dist_it = distance.find(to);
    if (dist_it == distance.end() ||
        std::isinf(dist_it->second)) {
        result.found = false;
        return result;
    }

    result.found = true;
    result.total_distance_km = dist_it->second;

    // 从终点回溯重建路径
    uint32_t current = to;
    while (current != from) {
        result.stations.push_back(current);
        current = previous.at(current);  // 路径上的节点一定在 previous 中
    }
    result.stations.push_back(from);
    std::reverse(result.stations.begin(), result.stations.end());

    // 写入缓存
    if (cache_valid_) {
        shortest_cache_[{from, to}] = result;
    }

    return result;
}

// ── 缓存管理 ──

void RailwayGraph::invalidateCache() {
    shortest_cache_.clear();
    cache_valid_ = true;  // 重新开始缓存
}
