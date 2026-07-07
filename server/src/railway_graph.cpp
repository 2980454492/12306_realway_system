// railway_graph.cpp — RailwayGraph 实现
#include "railway_graph.h"

#include <queue>
#include <algorithm>
#include <map>

// ── 构建 ──

void RailwayGraph::build(const std::vector<Line>& lines) {
    adjacency_.clear();
    invalidateCache();

    for (const auto& line : lines) {
        double dist = line.distance_km;

        // 双向边：A→B 和 B→A
        adjacency_[line.station_a].push_back({line.station_b, dist});
        adjacency_[line.station_b].push_back({line.station_a, dist});
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

    // 使用优先队列的 Dijkstra
    // pair<累计距离, 当前站ID>
    using QueueItem = std::pair<double, uint32_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pq;

    std::map<uint32_t, double> distance;
    std::map<uint32_t, uint32_t> previous;

    // 初始化所有距离为无穷大
    for (const auto& [station_id, _] : adjacency_) {
        distance[station_id] = std::numeric_limits<double>::infinity();
    }
    distance[from] = 0.0;
    pq.push({0.0, from});

    while (!pq.empty()) {
        auto [dist, current] = pq.top();
        pq.pop();

        // 跳过已处理过的更短路径
        if (dist > distance[current]) continue;

        if (current == to) {
            break;  // 找到目标，提前退出
        }

        // 遍历邻居
        auto adj_it = adjacency_.find(current);
        if (adj_it == adjacency_.end()) continue;

        for (const auto& [neighbor, edge_dist] : adj_it->second) {
            double new_dist = dist + edge_dist;
            if (new_dist < distance[neighbor]) {
                distance[neighbor] = new_dist;
                previous[neighbor] = current;
                pq.push({new_dist, neighbor});
            }
        }
    }

    // 重建路径
    if (distance[to] == std::numeric_limits<double>::infinity()) {
        result.found = false;
        return result;
    }

    result.found = true;
    result.total_distance_km = distance[to];

    // 从终点回溯到起点
    uint32_t current = to;
    while (current != from) {
        result.stations.push_back(current);
        current = previous[current];
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
