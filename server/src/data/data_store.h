// data_store.h — 数据存储层，管理站点/线路/列车的加载与查询
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <mutex>

/**
 * DataStore 单例 — 启动时从 JSON 文件加载种子数据到内存。
 * 提供按 ID / 名称查询的接口，后续 WAL 写操作也通过本类。
 * 线程安全：读操作用 shared_lock，写操作用 unique_lock。
 */
class DataStore {
public:
    static DataStore& instance();

    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    /** 从 config 目录加载全部数据。必须在使用前调用且仅调用一次 */
    bool initialize();

    /** 数据是否已加载 */
    bool isReady() const { return ready_; }

    // ── 查询接口 ──

    const std::vector<Station>& getAllStations() const { return stations_; }
    const std::vector<Line>& getAllLines() const { return lines_; }
    const std::vector<Train>& getAllTrains() const { return trains_; }

    const Station* getStation(uint32_t id) const;
    const Station* getStationByName(const std::string& name) const;
    const Line* getLine(uint32_t id) const;
    const Train* getTrain(const std::string& id) const;

    /** 按站点 ID 查找所有经过的列车（通过检查列车的停站序列） */
    std::vector<const Train*> getTrainsByStation(uint32_t station_id) const;

    /** 车站-线路-邻居索引：每个站在每条线路上的相邻站 */
    const std::map<uint32_t, std::vector<LineNeighbor>>& getStationLineIndex() const {
        return station_line_index_;
    }

    // ── 运行时变更（职工端）──

    /** 添加列车（审批通过后调用），自动重建索引 */
    void addTrain(const Train& train);

    /** 删除列车（标记为 ARCHIVED） */
    bool removeTrain(const std::string& train_id);

    /** 回写 trains 到 config/trains.json */
    bool saveTrains() const;

private:
    DataStore() = default;

    // ── 加载方法 ──
    bool loadStations();
    bool loadLines();
    bool loadTrains();
    void buildIndexes();
    void buildStationLineIndex();
    bool tryLoadStationLineIndex();
    void saveStationLineIndex() const;

    // ── 数据 ──
    std::vector<Station> stations_;
    std::vector<Line> lines_;
    std::vector<Train> trains_;

    // 索引：ID → vector 下标
    std::unordered_map<uint32_t, size_t> station_index_;
    std::unordered_map<uint32_t, size_t> line_index_;
    std::unordered_map<std::string, size_t> train_index_;
    std::unordered_map<std::string, uint32_t> station_name_to_id_;

    // 车站-线路-邻居索引：map<station_id, vector<LineNeighbor>>
    std::map<uint32_t, std::vector<LineNeighbor>> station_line_index_;

    bool ready_ = false;

    mutable std::mutex mutex_;  // Phase 2 用普通锁，Phase 5 升级为 shared_mutex
};
