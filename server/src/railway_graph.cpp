// railway_graph.cpp — RailwayGraph 实现
#include "railway_graph.h"
#include "data_store.h"
#include "geo_utils.h"

#include <queue>
#include <algorithm>
#include <map>

// ── 构建 ──

void RailwayGraph::build(const std::vector<Line>& lines) {
    adjacency_.clear();
    invalidateCache();

    auto& ds = DataStore::instance();

    for (const auto& line : lines) {
        if (line.stations.size() < 2) continue;

        // 按线路沿途站点序列拆分为多段，Haversine 计算邻站实际地理距离
        for (size_t i = 0; i + 1 < line.stations.size(); ++i) {
            auto* sa = ds.getStation(line.stations[i]);
            auto* sb = ds.getStation(line.stations[i + 1]);
            if (!sa || !sb) continue;

            double dist = haversineDist(*sa, *sb);

            adjacency_[line.stations[i]].push_back({line.stations[i + 1], dist});
            adjacency_[line.stations[i + 1]].push_back({line.stations[i], dist});
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
