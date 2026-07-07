// train_generator.cpp — 列车种子数据生成器实现
#include "train_generator.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <algorithm>

namespace {
    // 伪随机数生成器（固定种子确保每次生成结果一致）
    std::mt19937 rng(42);  // 固定种子，幂等

    int randomInt(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }

    // 各线路的中间停站序列（按线路编号 ID 映射）
    // 值为沿途站点 ID 列表（含端点，不含方向）
    const std::map<uint32_t, std::vector<uint32_t>> LINE_ROUTES = {
        // 京包高铁：呼和浩特东 → 察素齐 → 萨拉齐 → 包头东 → 包头
        {1, {1, 14, 13, 4, 3}},
        // 呼鄂城际：呼和浩特东 → 托克托东 → 准格尔 → 东胜西 → 鄂尔多斯
        {2, {1, 15, 11, 6, 5}},
        // 包西铁路：包头 → 达拉特西 → 东胜西 → 鄂尔多斯
        {3, {3, 12, 6, 5}},
        // 集包铁路：集宁南 → 卓资东 → 旗下营南 → 呼和浩特东 → 呼和浩特 → 察素齐 → 萨拉齐 → 包头东 → 包头
        {4, {7, 19, 20, 1, 2, 14, 13, 4, 3}},
        // 呼准鄂铁路：呼和浩特 → 托克托东 → 准格尔
        {5, {2, 15, 11}},
        // 包兰铁路(内蒙段)：包头 → 临河 → 乌海
        {6, {3, 9, 10}},
        // 集呼高铁：乌兰察布 → 卓资东 → 旗下营南 → 呼和浩特东
        {7, {8, 19, 20, 1}},
        // 呼临铁路：呼和浩特 → 包头东 → 包头 → 临河
        {8, {2, 4, 3, 9}},
        // 包白铁路：包头 → 包头东 → 白云鄂博
        {9, {3, 4, 17}},
        // 沿黄铁路：鄂尔多斯 → 东胜西 → 达拉特西 → 包头 → 临河 → 乌海
        {10, {5, 6, 12, 3, 9, 10}},
    };
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
    auto it = LINE_ROUTES.find(line.id);
    if (it == LINE_ROUTES.end()) {
        return;  // 找不到线路路径，跳过
    }

    auto route = it->second;  // LINE_ROUTES 中定义的正向停站序列（按地理方向）

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
            // 普速：长线用 K，短线用 K
            prefix = (line.distance_km > 200) ? "K" : "K";
            // 普速：硬卧 200，硬座 400，无座 100
            seats = {0, 0, 0, 200, 400, 100};
            base_departure = 700;    // 7:00
            trains_per_direction = 5;
            break;
    }

    // 生成两个方向的列车
    for (int dir = 0; dir < 2; ++dir) {
        std::vector<uint32_t> stops_sequence;
        if (dir == 0) {
            stops_sequence = route;  // 正向
        } else {
            stops_sequence = route;
            std::reverse(stops_sequence.begin(), stops_sequence.end());  // 反向
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

            // 发车时间：base + 每班间隔 90-180 分钟
            int departure_minutes = base_departure / 100 * 60 + base_departure % 100;
            departure_minutes += t * randomInt(90, 180);
            int departure_hhmm = (departure_minutes / 60 * 100) + (departure_minutes % 60);

            // 为每个停站计算到达和发车时间
            int current_time = departure_hhmm;
            for (size_t s = 0; s < stops_sequence.size(); ++s) {
                Stop stop;
                stop.station_id = stops_sequence[s];

                // 计算到下一站的运行时间（里程/速度*60，取整到分钟）
                int travel_minutes = 15;  // 默认站间距 15 分钟
                if (s > 0) {
                    // 用线路总里程比例估算行驶时间
                    double segment_ratio = 1.0 / (stops_sequence.size() - 1);
                    double speed_kmh = static_cast<double>(line.max_speed_kmh);
                    double distance = line.distance_km * segment_ratio;
                    // 高铁/城际实际旅行速度 ≈ 设计时速×0.7，普速 ≈ 设计时速×0.6
                    double factor = (line.type == LineType::NORMAL) ? 0.6 : 0.7;
                    travel_minutes = std::max(8, static_cast<int>(distance / (speed_kmh * factor) * 60));
                    // 最小 8 分钟：防止极短站间距（如 1km）导致计算值偏小
                }

                if (s == 0) {
                    // 始发站：无到达时间
                    stop.arrival = -1;
                    stop.departure = current_time;
                } else {
                    current_time += travel_minutes;
                    // 处理时间进位（分钟→小时）
                    int hours = current_time / 100;
                    int mins = current_time % 100;
                    mins += travel_minutes;
                    hours += mins / 60;
                    mins = mins % 60;
                    current_time = hours * 100 + mins;

                    stop.arrival = current_time;
                    if (s < stops_sequence.size() - 1) {
                        // 中间站：停留 2-5 分钟
                        int dwell = randomInt(2, 5);
                        current_time += dwell;
                        // 再次处理进位
                        hours = current_time / 100;
                        mins = current_time % 100;
                        if (mins >= 60) {
                            hours += mins / 60;
                            mins = mins % 60;
                        }
                        current_time = hours * 100 + mins;
                        stop.departure = current_time;
                    } else {
                        // 终到站：无发车时间
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
