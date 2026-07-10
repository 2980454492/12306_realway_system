// railway_graph.h — 铁路网图，邻接表 + Dijkstra 最短路径
#pragma once

#include "models.h"

#include <vector>
#include <map>
#include <set>
#include <optional>
#include <limits>

/**
 * 最短路径查询结果。
 */
struct PathResult {
    std::vector<uint32_t> stations;  // 路径上的站点序列
    double total_distance_km = 0.0;  // 总里程
    bool found = false;              // 是否找到路径
};

/**
 * RailwayGraph — 铁路网拓扑图。
 * 节点 = 站点，边 = 线路。
 * 支持 Dijkstra 最短路径查询（按里程），带缓存。
 */
class RailwayGraph {
public:
    RailwayGraph() = default;

    /** 根据线路列表构建邻接表，优先从本地缓存加载 */
    void build(const std::vector<Line>& lines);

    /** 查找 from → to 的最短路径（Dijkstra 按里程） */
    PathResult shortestPath(uint32_t from, uint32_t to) const;

    /** 使所有缓存失效（新增线路后调用，同时删除持久化文件） */
    void invalidateCache();

    /** 获取邻接表（调试用） */
    const std::map<uint32_t, std::map<uint32_t, double>>&
    getAdjacency() const { return adjacency_; }

    /** 图是否已构建 */
    bool isBuilt() const { return !adjacency_.empty(); }

private:
    /** 从 JSON 文件加载邻接表，失败返回 false */
    bool tryLoadFromFile();

    /** 将邻接表写入 JSON 文件 */
    void saveToFile() const;

    // 持久化文件路径
    static constexpr const char* GRAPH_FILE = "data/railway_graph.json";

    // 邻接表：站ID → {邻站ID: 距离km}。用 map 而非 vector<pair>：
    // 1. 自动去重——两站间多条线路只保留最短距离
    // 2. O(log n) 查找邻站，方便扩展到 2000+ 站点
    std::map<uint32_t, std::map<uint32_t, double>> adjacency_;

    // 最短路径缓存：key = pair(from, to), 失效标记由 invalidateCache 触发
    mutable std::map<std::pair<uint32_t, uint32_t>, PathResult> shortest_cache_;
    mutable bool cache_valid_ = true;
};
