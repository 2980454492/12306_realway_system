// train_generator.cpp — 列车种子数据生成器实现
#include "data/train_generator.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <algorithm>

namespace {
// 伪随机数生成器（固定种子确保每次生成结果一致）
std::mt19937 rng(42);  // 固定种子，幂等

// 生成 [min, max] 区间内的随机整数
int randomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}
}

// ── 公开接口 ──

std::vector<Train> TrainGenerator::generate(
    const std::vector<Line>& lines,
    const std::vector<Station>& /*stations*/)
{
    std::vector<Train> trains;
    int train_counter = 0;

    for (const auto& line : lines) {
        generateForLine(line, /*stations*/{}, trains, train_counter);
    }

    return trains;
}

// ── 内部实现 ──

void TrainGenerator::generateForLine(
    const Line& line,
    const std::vector<Station>& /*stations*/,
    std::vector<Train>& out_trains,
    int& train_counter)
{
    if (line.stations.size() < 2) return;  // 至少两个站才能生成列车

    const auto& route = line.stations;  // lines.json 中定义的沿途站点序列

    // 根据线路类型确定车次前缀和席位配置
    std::string prefix;
    SeatConfig seats;
    int base_departure = 600;  // 首班发车时间 HHMM
    int trains_per_direction = 0;

    switch (line.type) {
        case LineType::HIGH_SPEED:
            prefix = "G";
            // 高铁：商务座 10，一等座 80，二等座 400，无其他席位
            seats = {10, 80, 400, 0, 0, 50};
            base_departure = 600;    // 6:00 发首班
            trains_per_direction = 5;
            break;
        case LineType::INTERCITY:
            prefix = "C";
            // 城际：一等座 40，二等座 300
            seats = {0, 40, 300, 0, 0, 80};
            base_departure = 630;    // 6:30
            trains_per_direction = 5;
            break;
        case LineType::NORMAL:
            // 普速：长短线均用 K 字头
            prefix = "K";
            // 普速：硬卧 200，硬座 400，无座 100
            seats = {0, 0, 0, 200, 400, 100};
            base_departure = 700;    // 7:00
            trains_per_direction = 5;
            break;
    }

    // 生成两个方向的列车
    for (int dir = 0; dir < 2; ++dir) {
        // 全线路经过站序列（route_stations，用于票价里程计算）
        std::vector<uint32_t> full_route;
        if (dir == 0) {
            full_route = route;  // 正向
        } else {
            full_route = route;
            std::reverse(full_route.begin(), full_route.end());  // 反向
        }

        for (int t = 0; t < trains_per_direction; ++t) {
            Train train;
            // 车次号：前缀 + 4位数字
            std::ostringstream id_stream;
            id_stream << prefix << std::setfill('0') << std::setw(4)
                      << (1000 + train_counter * 7 + t * 13 + dir * 3) % 10000;
            train.id = id_stream.str();
            train.type = TrainType::REGULAR;
            train.status = TrainStatus::ACTIVE;
            train.seat_config = seats;

            // route_stations = 线路全序列（含所有经过站，不论是否停靠）
            train.route_stations = full_route;

            // stops = 办客停站序列（大站快车可能跳过部分中间站）
            // 线路站点 ≥ 5 的，30% 概率生成为大站快车（跳过 1-2 个中间站）
            std::vector<uint32_t> stop_ids = full_route;
            if (full_route.size() >= 5 && randomInt(1, 10) <= 3) {
                // 大站快车：随机去掉 1-2 个中间站（保留首尾）
                int skip_count = randomInt(1, std::min(2, static_cast<int>(full_route.size()) - 3));
                for (int sk = 0; sk < skip_count; ++sk) {
                    int idx = randomInt(1, static_cast<int>(stop_ids.size()) - 2);
                    stop_ids.erase(stop_ids.begin() + idx);
                }
            }

            // 发车时间：base + 每班间隔 90-180 分钟
            int departure_minutes = base_departure / 100 * 60 + base_departure % 100;
            departure_minutes += t * randomInt(90, 180);
            int departure_hhmm = (departure_minutes / 60 * 100) + (departure_minutes % 60);

            // 为每个停站计算到达和发车时间
            // 运行时间 = 线路总里程 / (速度×系数) / 经停段数 × 60
            double speed_kmh = static_cast<double>(line.max_speed_kmh);
            double factor = (line.type == LineType::NORMAL) ? 0.6 : 0.7;
            int total_travel = std::max(10, static_cast<int>(
                line.distance_km / (speed_kmh * factor) * 60));
            int segment_travel = total_travel / std::max(1, static_cast<int>(full_route.size()) - 1);

            int current_time = departure_hhmm;
            for (size_t s = 0; s < stop_ids.size(); ++s) {
                Stop stop;
                stop.station_id = stop_ids[s];
                int travel = (s == 0) ? 0 : std::max(3, segment_travel);

                if (s == 0) {
                    stop.arrival = -1;
                    stop.departure = current_time;
                } else {
                    // 处理时间进位
                    current_time += travel;
                    int hours = current_time / 100;
                    int mins = current_time % 100;
                    if (mins >= 60) { hours += 1; mins -= 60; }
                    current_time = hours * 100 + mins;

                    stop.arrival = current_time;
                    if (s < stop_ids.size() - 1) {
                        int dwell = randomInt(2, 5);
                        current_time += dwell;
                        hours = current_time / 100;
                        mins = current_time % 100;
                        if (mins >= 60) { hours += 1; mins -= 60; }
                        current_time = hours * 100 + mins;
                        stop.departure = current_time;
                    } else {
                        stop.departure = -1;
                    }
                }
                stop.platform = static_cast<uint8_t>(randomInt(1, 8));
                train.stops.push_back(stop);
            }

            out_trains.push_back(train);
            ++train_counter;
        }
    }
}
