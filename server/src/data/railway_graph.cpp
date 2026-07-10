// railway_graph.cpp — RailwayGraph 实现
#include "data/railway_graph.h"
#include "data/data_store.h"
#include "geo_utils.h"
#include "core/logger.h"

#include <queue>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// ── 持久化 ──

bool RailwayGraph::tryLoadFromFile() {
    if (!fs::exists(GRAPH_FILE)) return false;

    try {
        std::ifstream in(GRAPH_FILE);
        using json = nlohmann::json;
        json j;
        in >> j;

        adjacency_.clear();
        for (auto& [key, neighbors] : j.items()) {
            uint32_t u = std::stoul(key);
            for (auto& [nk, nv] : neighbors.items()) {
                adjacency_[u][std::stoul(nk)] = nv.get<double>();
            }
        }
        Logger::instance().info("Railway graph loaded from " + std::string(GRAPH_FILE));
        return true;
    } catch (const std::exception& e) {
        Logger::instance().warn(std::string("Failed to load graph: ") + e.what());
        return false;
    }
}

void RailwayGraph::saveToFile() const {
    try {
        using json = nlohmann::json;
        json j;
        for (const auto& [u, neighbors] : adjacency_) {
            json nj;
            for (const auto& [v, dist] : neighbors) {
                nj[std::to_string(v)] = dist;
            }
            j[std::to_string(u)] = nj;
        }
        std::ofstream out(GRAPH_FILE);
        out << j.dump();
        Logger::instance().info("Railway graph saved to " + std::string(GRAPH_FILE));
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save graph: ") + e.what());
    }
}

// ── 构建 ──

void RailwayGraph::build(const std::vector<Line>& lines) {
    // 优先从本地缓存加载
    if (tryLoadFromFile()) {
        invalidateCache();
        return;
    }

    adjacency_.clear();
    invalidateCache();

    auto& ds = DataStore::instance();

    for (const auto& line : lines) {
        if (line.stations.size() < 2) continue;

        for (size_t i = 0; i + 1 < line.stations.size(); ++i) {
            auto* sa = ds.getStation(line.stations[i]);
            auto* sb = ds.getStation(line.stations[i + 1]);
            if (!sa || !sb) continue;

            double dist = haversineDist(*sa, *sb);
            uint32_t u = line.stations[i];
            uint32_t v = line.stations[i + 1];

            auto& mu = adjacency_[u];
            auto it_u = mu.find(v);
            if (it_u == mu.end() || dist < it_u->second) mu[v] = dist;

            auto& mv = adjacency_[v];
            auto it_v = mv.find(u);
            if (it_v == mv.end() || dist < it_v->second) mv[u] = dist;
        }
    }

    saveToFile();
}

// ── Dijkstra 最短路径 ──

PathResult RailwayGraph::shortestPath(uint32_t from, uint32_t to) const {
    if (cache_valid_) {
        auto cache_key = std::make_pair(from, to);
        auto it = shortest_cache_.find(cache_key);
        if (it != shortest_cache_.end()) {
            return it->second;
        }
    }

    PathResult result;

    if (adjacency_.find(from) == adjacency_.end() ||
        adjacency_.find(to) == adjacency_.end()) {
        result.found = false;
        return result;
    }

    using QueueItem = std::pair<double, uint32_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pq;

    std::map<uint32_t, double> distance;
    std::map<uint32_t, uint32_t> previous;

    for (const auto& [station_id, _] : adjacency_) {
        distance[station_id] = std::numeric_limits<double>::infinity();
    }
    distance[from] = 0.0;
    pq.push({0.0, from});

    while (!pq.empty()) {
        auto [dist, current] = pq.top();
        pq.pop();

        if (dist > distance[current]) continue;
        if (current == to) break;

        auto adj_it = adjacency_.find(current);
        if (adj_it == adjacency_.end()) continue;

        for (const auto& [neighbor, edge_dist] : adj_it->second) {
            double new_dist = dist + edge_dist;
            if (new_dist < distance.at(neighbor)) {
                distance[neighbor] = new_dist;
                previous[neighbor] = current;
                pq.push({new_dist, neighbor});
            }
        }
    }

    auto dist_it = distance.find(to);
    if (dist_it == distance.end() || std::isinf(dist_it->second)) {
        result.found = false;
        return result;
    }

    result.found = true;
    result.total_distance_km = dist_it->second;

    uint32_t current = to;
    while (current != from) {
        result.stations.push_back(current);
        current = previous.at(current);
    }
    result.stations.push_back(from);
    std::reverse(result.stations.begin(), result.stations.end());

    if (cache_valid_) {
        shortest_cache_[{from, to}] = result;
    }

    return result;
}

// ── 缓存管理 ──

void RailwayGraph::invalidateCache() {
    shortest_cache_.clear();
    cache_valid_ = true;
    // 删除持久化文件，下次 build 会重建
    if (fs::exists(GRAPH_FILE)) {
        fs::remove(GRAPH_FILE);
    }
}
