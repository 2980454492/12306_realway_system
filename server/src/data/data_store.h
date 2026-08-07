// data_store.h — 数据存储层，管理站点/线路/列车的加载与查询
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <shared_mutex>

/** 车站-列车索引条目：某列车在停站序列中的位置 */
struct TrainStopEntry {
    std::string train_id;
    int stop_idx;
};

/** 车站-列车索引：站ID → 经过该站的所有列车条目 */
using StationTrainIndex = std::unordered_map<uint32_t, std::vector<TrainStopEntry>>;

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

    Station* getStation(uint32_t id);
    Train* getTrain(const std::string& id);
    Line* getLine(uint32_t id);

    /** 按站点 ID 查找所有经过的列车（通过检查列车的停站序列） */
    std::vector<const Train*> getTrainsByStation(uint32_t station_id) const;

    /** 车站-线路-邻居索引：每个站在每条线路上的相邻站 */
    const std::map<uint32_t, std::vector<LineNeighbor>>& getStationLineIndex() const {
        return station_line_index_;
    }

    /** 车站-列车索引：站ID → 经过该站的所有列车（启动时构建，O(1) 查票） */
    const StationTrainIndex& getStationTrainIndex() const {
        return station_train_index_;
    }

    /** 站名/城市名集合（预建，O(1) 校验合法性） */
    const std::unordered_set<std::string>& getStationNameSet() const {
        return station_name_set_;
    }

    /** 城市名 → 全部站 ID（一个城市可能有多个站） */
    const std::vector<uint32_t>& getStationIdsByCity(const std::string& city) const {
        static const std::vector<uint32_t> empty;
        std::unordered_map<std::string, std::vector<uint32_t>>::const_iterator it = city_to_ids_.find(city);
        return (it != city_to_ids_.end()) ? it->second : empty;
    }

    /** 站名 → 站 ID（预建，O(1) 按站名查站） */
    uint32_t stationNameToId(const std::string& name) const {
        std::unordered_map<std::string, uint32_t>::const_iterator it = name_to_id_.find(name);
        return (it != name_to_id_.end()) ? it->second : 0;
    }

    // ── 运行时变更（职工端）──

    /** 添加列车（审批通过后调用），自动重建索引 */
    void addTrain(const Train& train);

    /** 删除列车（标记为 ARCHIVED） */
    bool removeTrain(const std::string& train_id);

    /** 回写 trains 到 config/trains.json */
    bool saveTrains();

    // ── 站点管理（INFRA_ADMIN）──

    /** 添加站点，自动分配 ID 并写入 station 的 id 字段，返回是否成功 */
    bool addStation(Station& station);

    /** 更新站点 */
    bool updateStation(uint32_t id, const Station& updated);

    /** 删除站点 */
    bool removeStation(uint32_t id);

    /** 回写站点到 config/stations.json */
    bool saveStations() const;

    // ── 线路管理（INFRA_ADMIN）──

    /** 添加线路，自动分配 ID 并写入 line 的 id 字段，返回是否成功 */
    bool addLine(Line& line);

    /** 更新线路 */
    bool updateLine(uint32_t id, const Line& updated);

    /** 删除线路 */
    bool removeLine(uint32_t id);

    /** 回写线路到 config/lines.json */
    bool saveLines() const;

private:
    DataStore() = default;

    // ── 加载方法 ──
    bool loadStations();
    bool loadLines();
    bool loadTrains();

    // —— 构建索引 ——
    void buildIndexes();

    // ── 数据 ──
    std::vector<Station> stations_;
    std::vector<Line> lines_;
    std::vector<Train> trains_;

    // 车站 ID → vector
    std::unordered_map<uint32_t, size_t> station_index_;
    // 列车 ID → vector
    std::unordered_map<std::string, size_t> train_index_;
    // 线路 ID → vector
    std::unordered_map<uint32_t, size_t> line_index_;
    
    // 城市名 → 车站 IDs 
    std::unordered_map<std::string, std::vector<uint32_t>> city_to_ids_;
    // 站名 → 车站 ID
    std::unordered_map<std::string, uint32_t> name_to_id_;
    // 车站-线路-邻居索引：map<station_id, vector<LineNeighbor>>
    std::map<uint32_t, std::vector<LineNeighbor>> station_line_index_;
    // 车站-列车索引：站ID → 经过该站的列车条目
    StationTrainIndex station_train_index_;

    // 站名/城市名 → 存在性
    std::unordered_set<std::string> station_name_set_;

    bool ready_ = false;

    mutable std::shared_mutex mutex_;  // 读共享，写独占
};
