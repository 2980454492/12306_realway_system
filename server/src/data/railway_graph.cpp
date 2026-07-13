// railway_graph.cpp — RailwayGraph 实现
#include "data/railway_graph.h"
#include "data/data_store.h"
#include "core/config.h"
#include "core/utils.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// ── 持久化 ──

bool RailwayGraph::tryLoadFromFile() {
    if (!fs::exists(config::RAILWAY_GRAPH_FILE)) return false;

    try {
        std::ifstream in(config::RAILWAY_GRAPH_FILE);
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
        Logger::instance().info("Railway graph loaded from " + std::string(config::RAILWAY_GRAPH_FILE));
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
        std::ofstream out(config::RAILWAY_GRAPH_FILE);
        out << j.dump();
        Logger::instance().info("Railway graph saved to " + std::string(config::RAILWAY_GRAPH_FILE));
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save graph: ") + e.what());
    }
}

// ── 构建 ──

void RailwayGraph::build(const std::vector<Line>& lines) {
    if (tryLoadFromFile()) return;

    adjacency_.clear();

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

// ── 缓存管理 ──

void RailwayGraph::invalidateCache() {
    if (fs::exists(config::RAILWAY_GRAPH_FILE)) {
        fs::remove(config::RAILWAY_GRAPH_FILE);
    }
}
