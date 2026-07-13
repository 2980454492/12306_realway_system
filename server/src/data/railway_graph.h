// railway_graph.h — 铁路网拓扑图（邻接表 + JSON 持久化）
#pragma once

#include "models.h"

#include <vector>
#include <map>
#include <set>
#include <string>
#include <optional>
#include <limits>

/**
 * RailwayGraph — 铁路网拓扑图，节点=站点，边=线路。
 * 邻接表持久化到本地 JSON，后续 Phase 8 路网可视化导出使用。
 */
class RailwayGraph {
public:
    RailwayGraph() = default;

    /** 根据线路列表构建邻接表，优先从本地缓存加载 */
    void build(const std::vector<Line>& lines);

    /** 使所有缓存失效（新增线路后调用，同时删除持久化文件） */
    void invalidateCache();

    /** 获取邻接表 */
    const std::map<uint32_t, std::map<uint32_t, double>>&
    getAdjacency() const { return adjacency_; }

    /** 图是否已构建 */
    bool isBuilt() const { return !adjacency_.empty(); }

private:
    bool tryLoadFromFile();
    void saveToFile() const;

    std::map<uint32_t, std::map<uint32_t, double>> adjacency_;
};
