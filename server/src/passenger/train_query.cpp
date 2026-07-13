// train_query.cpp — 列车余票查询实现
#include "passenger/train_query.h"
#include "data/data_store.h"
#include "passenger/seat_inventory.h"
#include "core/config.h"
#include "core/utils.h"
#include "core/logger.h"

#include <set>
#include <tuple>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

// ── 索引类型定义 ──

/** 索引中的一条记录：某列车在停站序列中的位置 */
struct TrainStopEntry {
    const Train* train;
    int stop_idx;  // train->stops 中的下标
};

/**
 * 车站-列车索引：站ID → 经过该站的所有列车条目。
 * 换乘查询的核心数据结构，构建一次，O(1) 查找。
 */
using StationTrainIndex = std::unordered_map<uint32_t, std::vector<TrainStopEntry>>;

// ── 索引持久化 ──

/** 将索引序列化为 JSON 并写入文件。key 用车站 ID（转字符串），value 为 [{train_id, stop_idx}] */
void saveIndex(const std::string& path, const StationTrainIndex& idx) {
    using json = nlohmann::json;
    json j;
    for (const auto& [station_id, entries] : idx) {
        json arr = json::array();
        for (const auto& [train, stop_idx] : entries) {
            arr.push_back({{"train_id", train->id}, {"stop_idx", stop_idx}});
        }
        j[std::to_string(station_id)] = arr;
    }
    std::ofstream out(path);
    out << j.dump();
}

/**
 * 从 JSON 文件加载索引，train 指针通过 DataStore 解析。
 * 文件不存在或损坏时返回 nullopt，调用方回退到全量构建。
 */
std::optional<StationTrainIndex> loadIndex(const std::string& path, DataStore& ds) {
    using json = nlohmann::json;
    if (!fs::exists(path)) return std::nullopt;

    try {
        std::ifstream in(path);
        json j;
        in >> j;

        StationTrainIndex idx;
        for (auto& [key, arr] : j.items()) {
            uint32_t station_id = std::stoul(key);
            auto& vec = idx[station_id];
            for (auto& entry : arr) {
                std::string train_id = entry["train_id"].get<std::string>();
                int stop_idx = entry["stop_idx"].get<int>();
                auto* train = ds.getTrain(train_id);
                if (train && train->status == TrainStatus::ACTIVE) {
                    vec.push_back({train, stop_idx});
                }
            }
        }
        return idx;
    } catch (const std::exception& e) {
        Logger::instance().warn(std::string("Failed to load index: ") + e.what());
        return std::nullopt;
    }
}

// ── 工具函数 ──

/** 计算票价（元），基准为二等座每公里费率 */
double calcPrice(double distance_km, SeatType seat_type) {
    return distance_km * BASE_RATE_PER_KM * seatPriceMultiplier(seat_type);
}

/** 查某车次从 from 站到 to 站的可用座位（仅查数量，不锁定） */
SeatConfig getAvailableSeats(const std::string& train_id, const std::string& date) {
    return SeatInventory::instance().getAvailable(train_id, date);
}

// ── 车站-列车索引 ──

/** 构建车站-列车索引（全量扫描全部列车） */
StationTrainIndex buildStationTrainIndex(DataStore& ds) {
    StationTrainIndex idx;
    for (const auto& train : ds.getAllTrains()) {
        if (train.status != TrainStatus::ACTIVE) continue;
        for (size_t i = 0; i < train.stops.size(); ++i) {
            idx[train.stops[i].station_id].push_back({&train, static_cast<int>(i)});
        }
    }
    return idx;
}

/**
 * 初始化索引：优先从 JSON 加载，失败则全量构建并持久化。
 * 只被 getStationIndex() 调用一次。
 */
StationTrainIndex initStationIndex(DataStore& ds) {
    auto loaded = loadIndex(config::STATION_TRAIN_INDEX_FILE, ds);
    if (loaded) {
        Logger::instance().info("Station-train index loaded from file");
        return *loaded;
    }
    Logger::instance().info("Building station-train index...");
    auto idx = buildStationTrainIndex(ds);
    saveIndex(config::STATION_TRAIN_INDEX_FILE, idx);
    Logger::instance().info("Station-train index saved to " + std::string(config::STATION_TRAIN_INDEX_FILE));
    return idx;
}

/**
 * 获取车站-列车索引（懒初始化 + 文件持久化）。
 * C++11 static 局部变量保证线程安全的单次初始化。
 */
StationTrainIndex& getStationIndex(DataStore& ds) {
    static StationTrainIndex idx = initStationIndex(ds);
    return idx;
}

/**
 * 检查中转站是否在地理上处于 from 和 to 之间。
 * 两段各自的直线距离不应大幅超过 from→to 的直线距离，
 * 防止绕路太远的中转方案（如呼和浩特→北京→包头）。
 */
bool isTransferBetween(uint32_t from, uint32_t transfer, uint32_t to, DataStore& ds) {
    auto* s_from = ds.getStation(from);
    auto* s_trans = ds.getStation(transfer);
    auto* s_to = ds.getStation(to);
    if (!s_from || !s_trans || !s_to) return false;

    double direct = haversineDist(*s_from, *s_to);
    double leg1   = haversineDist(*s_from, *s_trans);
    double leg2   = haversineDist(*s_trans, *s_to);

    // 单段不超过直达距离，总里程不超过直达 2 倍
    return leg1 <= direct && leg2 <= direct && (leg1 + leg2) <= direct * 2.0;
}

}  // namespace

/** 预热索引：触发 getStationIndex 的 static 懒惰初始化（由 TrainQuery::initialize 调用） */
void TrainQuery::initialize() {
    getStationIndex(DataStore::instance());
}

// ── 公开接口 ──

QueryResult TrainQuery::query(uint32_t from_station, uint32_t to_station,
                               const std::string& date) {
    QueryResult result;
    auto& ds = DataStore::instance();

    auto& stationIndex = getStationIndex(ds);

    // ── 直达查询 ──
    auto from_it = stationIndex.find(from_station);
    if (from_it != stationIndex.end()) {
        for (const auto& [train, from_idx] : from_it->second) {
            if (train->status != TrainStatus::ACTIVE) continue;

            // 找 to 在停站序列中的位置（必须在 from 之后）
            int to_idx = -1;
            for (size_t i = from_idx + 1; i < train->stops.size(); ++i) {
                if (train->stops[i].station_id == to_station) {
                    to_idx = static_cast<int>(i);
                    break;
                }
            }
            if (to_idx < 0) continue;

            // 查当天车次时，过滤已发车的
            int dep_hhmm = train->stops[from_idx].departure;
            if (dep_hhmm > 0 && isToday(date) && nowHHMM() > dep_hhmm) continue;

            QueryResultItem item;
            item.train_id = train->id;
            item.train_type = train->type;
            item.from_station = from_station;
            item.to_station = to_station;
            item.departure_time = train->stops[from_idx].departure;
            item.arrival_time = train->stops[to_idx].arrival;
            item.duration_minutes = timeDiff(item.departure_time, item.arrival_time);
            item.stops = train->stops;

            double trip_km = calcRouteDistance(*train, from_station, to_station, ds);
            item.distance_km = trip_km;
            item.price = calcPrice(trip_km, SeatType::SECOND);

            item.available_seats = getAvailableSeats(train->id, date);

            result.direct.push_back(item);
        }
    }

    // ── 换乘查询 ──
    // 思路：遍历 from 站出发的列车，将其后续停站作为候选中转站，
    //       通过索引 O(1) 查找从中转站到 to 的列车。
    auto to_it = stationIndex.find(to_station);
    if (from_it != stationIndex.end() && to_it != stationIndex.end()) {
        std::set<std::string> seen_pairs;  // 防止同一对 (T1, T2) 重复

        for (const auto& [train1, from_idx] : from_it->second) {
            if (train1->status != TrainStatus::ACTIVE) continue;

            // 第一程已发车则跳过
            int dep_hhmm = train1->stops[from_idx].departure;
            if (dep_hhmm > 0 && isToday(date) && nowHHMM() > dep_hhmm) continue;

            // T1 在 from 站之后的所有停站都可以作为中转候选
            for (size_t traini = from_idx + 1; traini < train1->stops.size(); ++traini) {
                uint32_t transfer_id = train1->stops[traini].station_id;
                if (transfer_id == to_station) continue;  // 直达已处理
                int arrival_at_transfer = train1->stops[traini].arrival;

                // 地理约束：中转站须在 from 和 to 之间
                if (!isTransferBetween(from_station, transfer_id, to_station, ds)) continue;

                // 查经过该中转站、能到 to 的列车
                auto trans_it = stationIndex.find(transfer_id);
                if (trans_it == stationIndex.end()) continue;

                for (const auto& [train2, trans_idx] : trans_it->second) {
                    if (train2->status != TrainStatus::ACTIVE) continue;
                    if (train1->id == train2->id) continue;  // 同车次不算换乘

                    // T2 从中转站之后必须能到 to
                    int to_idx2 = -1;
                    for (size_t j = trans_idx + 1; j < train2->stops.size(); ++j) {
                        if (train2->stops[j].station_id == to_station) {
                            to_idx2 = static_cast<int>(j);
                            break;
                        }
                    }
                    if (to_idx2 < 0) continue;

                    // 去重
                    std::string pair_key = train1->id + "|" + train2->id;
                    if (seen_pairs.count(pair_key)) continue;
                    seen_pairs.insert(pair_key);

                    int departure_from_transfer = train2->stops[trans_idx].departure;

                    // 换乘时间窗口 ≥ 10 分钟且 ≤ 3 小时
                    int gap = timeDiff(arrival_at_transfer, departure_from_transfer);
                    if (gap < 10 || gap > 180) continue;

                    QueryResultItem item;
                    item.train_id = train1->id + " → " + train2->id;
                    item.train_type = train1->type;
                    item.from_station = from_station;
                    item.to_station = to_station;
                    item.departure_time = train1->stops[from_idx].departure;
                    item.arrival_time = train2->stops[to_idx2].arrival;
                    item.duration_minutes = timeDiff(item.departure_time, item.arrival_time);
                    item.stops = train1->stops;
                    item.is_transfer = true;
                    auto* st = ds.getStation(transfer_id);
                    item.transfer_station = st ? st->name : "unknown";
                    item.second_train_id = train2->id;
                    item.second_stops = train2->stops;
                    item.transfer_arrival_time = arrival_at_transfer;
                    item.transfer_departure_time = departure_from_transfer;
                    item.transfer_gap_minutes = gap;

                    double km1 = calcRouteDistance(*train1, from_station, transfer_id, ds);
                    double km2 = calcRouteDistance(*train2, transfer_id, to_station, ds);
                    item.distance_km = km1 + km2;
                    item.price = calcPrice(km1 + km2, SeatType::SECOND);
                    item.first_leg_price = calcPrice(km1, SeatType::SECOND);
                    item.second_leg_price = calcPrice(km2, SeatType::SECOND);
                    item.first_leg_seats = getAvailableSeats(train1->id, date);
                    item.second_leg_seats = getAvailableSeats(train2->id, date);

                    result.transfers.push_back(item);
                }
            }
        }
    }

    return result;
}

// ── 车站查询 ──

std::vector<StationQueryItem> TrainQuery::queryByStation(uint32_t station_id) {
    std::vector<StationQueryItem> result;
    auto& ds = DataStore::instance();
    const auto& idx = getStationIndex(ds);
    auto it = idx.find(station_id);
    if (it == idx.end()) return result;

    for (const auto& [train, stop_idx] : it->second) {
        if (train->status != TrainStatus::ACTIVE) continue;

        StationQueryItem item;
        item.train_id = train->id;
        item.train_type = train->type;
        item.stops = train->stops;

        // 始发站名 / 终到站名
        if (!train->stops.empty()) {
            auto* orig = ds.getStation(train->stops.front().station_id);
            auto* term = ds.getStation(train->stops.back().station_id);
            item.from_station_name = orig ? orig->name : "";
            item.to_station_name = term ? term->name : "";
        }

        // 到达/发车时刻
        const auto& stop = train->stops[stop_idx];
        item.arrival_time = stop.arrival;     // -1 表示始发站
        item.departure_time = stop.departure; // -1 表示终到站
        result.push_back(item);
    }

    // 默认按发车时间升序，终到站按到达时间
    std::sort(result.begin(), result.end(),
        [](const StationQueryItem& a, const StationQueryItem& b) {
            int ta = (a.departure_time > 0) ? a.departure_time : a.arrival_time;
            int tb = (b.departure_time > 0) ? b.departure_time : b.arrival_time;
            return ta < tb;
        });

    return result;
}

// ── 多站查询（合并 + 排序）──

std::vector<StationQueryItem> TrainQuery::queryByStations(
    const std::vector<uint32_t>& station_ids, const std::string& sort) {

    auto& ds = DataStore::instance();
    const auto& idx = getStationIndex(ds);

    // 1. 收集所有 (train, stop) 条目
    std::unordered_map<uint32_t, std::string> station_names;
    struct RawEntry { const Train* train; int stop_idx; uint32_t station_id; };
    std::vector<RawEntry> raw;
    for (auto sid : station_ids) {
        station_names[sid] = ds.getStation(sid) ? ds.getStation(sid)->name : "";
        auto it = idx.find(sid);
        if (it == idx.end()) continue;
        for (const auto& [train, stop_idx] : it->second) {
            if (train->status == TrainStatus::ACTIVE) {
                raw.push_back({train, stop_idx, sid});
            }
        }
    }

    // 2. 同车次合并：始发 > 终到 > 先停靠
    std::unordered_map<std::string, RawEntry> best;
    for (const auto& re : raw) {
        auto& train = *re.train;
        auto it = best.find(train.id);
        if (it == best.end()) {
            best[train.id] = re;
            continue;
        }
        const auto& old = it->second;
        uint32_t orig_id = train.stops.front().station_id;
        uint32_t term_id = train.stops.back().station_id;
        bool oldOrig = (old.station_id == orig_id);
        bool newOrig = (re.station_id == orig_id);
        bool oldTerm = (old.station_id == term_id);
        bool newTerm = (re.station_id == term_id);
        if (oldOrig) continue;  // 已有始发，不换
        if (newOrig || (newTerm && !oldTerm) || (!oldTerm && re.stop_idx < old.stop_idx)) {
            best[train.id] = re;
        }
    }

    // 3. 构建结果
    std::vector<StationQueryItem> result;
    for (const auto& [tid, re] : best) {
        const auto& train = *re.train;
        StationQueryItem item;
        item.train_id = train.id;
        item.train_type = train.type;
        item.stops = train.stops;
        item.station_id = re.station_id;
        item.station_name = station_names[re.station_id];
        if (!train.stops.empty()) {
            auto* orig = ds.getStation(train.stops.front().station_id);
            auto* term = ds.getStation(train.stops.back().station_id);
            item.from_station_name = orig ? orig->name : "";
            item.to_station_name = term ? term->name : "";
        }
        const auto& stop = train.stops[re.stop_idx];
        item.arrival_time = stop.arrival;
        item.departure_time = stop.departure;
        result.push_back(item);
    }

    // 4. 排序
    if (sort == "train_id") {
        std::sort(result.begin(), result.end(),
            [](const StationQueryItem& a, const StationQueryItem& b) {
                return a.train_id < b.train_id;
            });
    } else {
        std::sort(result.begin(), result.end(),
            [](const StationQueryItem& a, const StationQueryItem& b) {
                int ta = (a.departure_time > 0) ? a.departure_time : a.arrival_time;
                int tb = (b.departure_time > 0) ? b.departure_time : b.arrival_time;
                return ta < tb;
            });
    }

    return result;
}
