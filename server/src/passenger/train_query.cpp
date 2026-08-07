// train_query.cpp — 列车余票查询实现
#include "passenger/train_query.h"
#include "data/data_store.h"
#include "passenger/seat_inventory.h"
#include "sys_admin/system_config.h"
#include "utils.h"
#include "system/logger.h"

#include <set>
#include <tuple>
#include <unordered_map>

namespace {

struct BestEntry {
    int stop_idx;
    uint32_t station_id;
    const Train* train;
};

// ── 工具函数 ──

/**
 * 检查中转站是否在地理上处于 from 和 to 之间。
 * 两段各自的直线距离不应大幅超过 from→to 的直线距离，
 * 防止绕路太远的中转方案。
 */
bool isTransferBetween(uint32_t from, uint32_t transfer, uint32_t to, DataStore& ds) {
    const Station* s_from = ds.getStation(from);
    const Station* s_trans = ds.getStation(transfer);
    const Station* s_to = ds.getStation(to);
    if (!s_from || !s_trans || !s_to) return false;

    double direct = haversineDist(*s_from, *s_to);
    double leg1   = haversineDist(*s_from, *s_trans);
    double leg2   = haversineDist(*s_trans, *s_to);

    // 单段不超过直达距离，总里程不超过直达 2 倍
    return leg1 <= direct && leg2 <= direct && (leg1 + leg2) <= direct * 2.0;
}

}  // namespace

/** 启动时无操作（索引改为每次查询按需重建） */
void TrainQuery::initialize() {}

// ── 公开接口 ──

QueryResult TrainQuery::query(uint32_t from_station, uint32_t to_station,
                               const std::string& date) {
    QueryResult result;
    DataStore& ds = DataStore::instance();
    SeatInventory& si = SeatInventory::instance();
    const StationTrainIndex& stationIndex = ds.getStationTrainIndex();

    // ── 直达查询 ──
    StationTrainIndex::const_iterator from_it = stationIndex.find(from_station);
    if (from_it != stationIndex.end()) {
        for (const TrainStopEntry& entry : from_it->second) {
            const std::string& train_id = entry.train_id;
            int from_idx = entry.stop_idx;
            Train* train = ds.getTrain(train_id);
            if (!train || train->status != TrainStatus::ACTIVE) continue;
            // 日期过滤：valid_from 未到或 valid_until 已过则跳过
            if (!train->valid_from.empty() && train->valid_from > date) continue;
            if (!train->valid_until.empty() && train->valid_until < date) continue;

            // 找 to 在停站序列中的位置（必须在 from 之后）
            int to_idx = -1;
            for (size_t i = from_idx + 1; i < train->stops.size(); ++i) {
                if (train->stops[i].station_id == to_station) {
                    to_idx = static_cast<int>(i);
                    break;
                }
            }
            if (to_idx < 0) continue;

            // 通过站不办客，不可作为乘车站或到达站
            if (train->stops[from_idx].stop_type == StopType::PASS
                || train->stops[to_idx].stop_type == StopType::PASS)
                continue;

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
            item.price = trip_km * pricePerKm(train->id, SeatType::SECOND);

            item.available_seats = si.getAvailable(train->id, date);

            result.direct.push_back(item);
        }
    }

    // ── 换乘查询 ──
    // 思路：遍历 from 站出发的列车，将其后续停站作为候选中转站，
    //       通过索引 O(1) 查找从中转站到 to 的列车。
    StationTrainIndex::const_iterator to_it = stationIndex.find(to_station);
    if (from_it != stationIndex.end() && to_it != stationIndex.end()) {
        std::set<std::string> seen_pairs;  // 防止同一对 (T1, T2) 重复

        for (const TrainStopEntry& entry1 : from_it->second) {
            const std::string& train1_id = entry1.train_id;
            int from_idx = entry1.stop_idx;
            Train* train1 = ds.getTrain(train1_id);
            if (!train1 || train1->status != TrainStatus::ACTIVE) continue;
            if (!train1->valid_from.empty() && train1->valid_from > date) continue;
            if (!train1->valid_until.empty() && train1->valid_until < date) continue;

            // 第一程已发车则跳过；通过站不可上车
            if (train1->stops[from_idx].stop_type == StopType::PASS) continue;
            int dep_hhmm = train1->stops[from_idx].departure;
            if (dep_hhmm > 0 && isToday(date) && nowHHMM() > dep_hhmm) continue;

            // T1 在 from 站之后的所有可下车/换乘的站作为中转候选（跳过通过站）
            for (size_t traini = from_idx + 1; traini < train1->stops.size(); ++traini) {
                if (train1->stops[traini].stop_type == StopType::PASS) continue;  // 通过站不可下车
                uint32_t transfer_id = train1->stops[traini].station_id;
                if (transfer_id == to_station) continue;  // 直达已处理
                int arrival_at_transfer = train1->stops[traini].arrival;

                // 地理约束：中转站须在 from 和 to 之间
                if (!isTransferBetween(from_station, transfer_id, to_station, ds)) continue;

                // 查经过该中转站、能到 to 的列车
                StationTrainIndex::const_iterator trans_it = stationIndex.find(transfer_id);
                if (trans_it == stationIndex.end()) continue;

                for (const TrainStopEntry& entry2 : trans_it->second) {
                    const std::string& train2_id = entry2.train_id;
                    int trans_idx = entry2.stop_idx;
                    Train* train2 = ds.getTrain(train2_id);
                    if (!train2 || train2->status != TrainStatus::ACTIVE) continue;
                    if (!train2->valid_from.empty() && train2->valid_from > date) continue;
                    if (!train2->valid_until.empty() && train2->valid_until < date) continue;
                    if (train1->id == train2->id) continue;  // 同车次不算换乘

                    // 通过站不可上车或下车
                    if (train2->stops[trans_idx].stop_type == StopType::PASS) continue;

                    // T2 从中转站之后必须能到 to
                    int to_idx2 = -1;
                    for (size_t j = trans_idx + 1; j < train2->stops.size(); ++j) {
                        if (train2->stops[j].station_id == to_station) {
                            to_idx2 = static_cast<int>(j);
                            break;
                        }
                    }
                    if (to_idx2 < 0) continue;
                    if (train2->stops[to_idx2].stop_type == StopType::PASS) continue;  // 通过站不可作为到达站

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
                    const Station* transfer_st = ds.getStation(transfer_id);
                    item.transfer_station = transfer_st ? transfer_st->name : "unknown";
                    item.second_train_id = train2->id;
                    item.second_stops = train2->stops;
                    item.transfer_arrival_time = arrival_at_transfer;
                    item.transfer_departure_time = departure_from_transfer;
                    item.transfer_gap_minutes = gap;

                    double km1 = calcRouteDistance(*train1, from_station, transfer_id, ds);
                    double km2 = calcRouteDistance(*train2, transfer_id, to_station, ds);
                    item.distance_km = km1 + km2;
                    item.first_leg_price = km1 * pricePerKm(train1->id, SeatType::SECOND);
                    item.second_leg_price = km2 * pricePerKm(train2->id, SeatType::SECOND);
                    item.price = item.first_leg_price + item.second_leg_price;
                    item.first_leg_seats = si.getAvailable(train1->id, date);
                    item.second_leg_seats = si.getAvailable(train2->id, date);

                    result.transfers.push_back(item);
                }
            }
        }
    }

    return result;
}

// ── 车站查询 ──

// ── 多站查询（合并 + 排序）──

std::vector<StationQueryItem> TrainQuery::queryByStations(
    const std::vector<uint32_t>& station_ids, const std::string& sort) {
    DataStore& ds = DataStore::instance();
    const StationTrainIndex& idx = ds.getStationTrainIndex();

    // 1. 一次遍历：收集同车次最优停站，同时缓存 train 指针 + 站点名
    std::unordered_map<std::string, BestEntry> best;
    for (uint32_t sid : station_ids) {
        StationTrainIndex::const_iterator it = idx.find(sid);
        if (it == idx.end()) continue;
        for (const TrainStopEntry& entry : it->second) {
            const std::string& train_id = entry.train_id;
            int stop_idx = entry.stop_idx;
            const Train* t = ds.getTrain(train_id);
            if (!t || t->status != TrainStatus::ACTIVE) continue;
            std::unordered_map<std::string, BestEntry>::const_iterator bi = best.find(train_id);
            if (bi == best.end() || stop_idx < bi->second.stop_idx)
                best[train_id] = {stop_idx, sid, t};
        }
    }

    // 2. 构建结果（复用第一步缓存的 train 指针 + 内联查站名）
    std::vector<StationQueryItem> result;
    for (std::unordered_map<std::string, BestEntry>::const_iterator itb = best.begin();
         itb != best.end(); ++itb) {
        const std::string& tid = itb->first;
        const BestEntry& re = itb->second;
        const Train* train = re.train;
        StationQueryItem item;
        item.train_id = tid;
        item.train_type = train->type;
        item.stops = train->stops;
        item.station_id = re.station_id;
        const Station* st = ds.getStation(re.station_id);
        item.station_name = st ? st->name : "";
        const Stop& stop = train->stops[re.stop_idx];
        item.arrival_time = stop.arrival;
        item.departure_time = stop.departure;
        if (!train->stops.empty()) {
            const Station* orig = ds.getStation(train->stops.front().station_id);
            const Station* term = ds.getStation(train->stops.back().station_id);
            item.from_station_name = orig ? orig->name : "";
            item.to_station_name = term ? term->name : "";
        }
        result.push_back(item);
    }

    // 3. 排序
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

/** 构建席位价格 JSON：按列车 ID 前缀查费率矩阵，逐席位类型计算票价。
 *  费率为 0 的席位（如硬卧列车不设商务座）不输出。 */
nlohmann::json TrainQuery::buildSeatPrices(const std::string& train_id, double distance_km) {
    nlohmann::json sp;
    SystemConfig& cfg = SystemConfig::instance();

    const char* names[] = {"BUSINESS", "FIRST", "SECOND",
                           "HARD_SLEEPER", "HARD_SEAT", "NO_SEAT"};
    SeatType types[] = {SeatType::BUSINESS, SeatType::FIRST, SeatType::SECOND,
                        SeatType::HARD_SLEEPER, SeatType::HARD_SEAT, SeatType::NO_SEAT};

    for (int i = 0; i < 6; i++) {
        double rate = cfg.ratePerKm(train_id, types[i]);
        if (rate > 0)
            sp[names[i]] = std::round(distance_km * rate * 100) / 100;
    }
    return sp;
}