// order_service.cpp — OrderService 实现
#include "order_service.h"
#include "seat_inventory.h"
#include "data_store.h"
#include "geo_utils.h"
#include "logger.h"

#include <unordered_map>

#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace {
    // ── UUID 生成 ──
    std::string makeUuid() {
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_int_distribution<uint64_t> dist;
        uint64_t a = dist(rng), b = dist(rng);
        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8) << ((a >> 32) & 0xFFFFFFFF)
            << "-" << std::setw(4) << ((a >> 16) & 0xFFFF)
            << "-4" << std::setw(3) << (a & 0xFFF)
            << "-8" << std::setw(3) << ((b >> 48) & 0xFFF)
            << "-" << std::setw(4) << ((b >> 32) & 0xFFFF)
            << std::setw(8) << (b & 0xFFFFFFFF);
        return oss.str();
    }

    // ── 当前时间 ISO 8601 ──
    std::string nowIso() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    // ── 当前 HHMM ──
    int nowHHMM() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::gmtime(&t);
        return tm->tm_hour * 100 + tm->tm_min;
    }
}

// ── 单例 ──

OrderService& OrderService::instance() {
    static OrderService svc;
    return svc;
}

// ── 购票 ──

OrderService::OrderResult OrderService::createOrder(
    const std::string& user_id,
    const std::string& train_id,
    const std::string& date,
    uint32_t from_station,
    uint32_t to_station,
    SeatType seat_type,
    int count,
    const std::string& passenger_name,
    const std::string& passenger_id)
{
    OrderResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 校验列车存在且运行中
    auto* train = DataStore::instance().getTrain(train_id);
    if (!train || train->status != TrainStatus::ACTIVE) {
        result.error = "Train not found or not active";
        return result;
    }

    // 2. 校验停站序列 + 记录位置（用于逐段累加里程）
    int from_idx = -1, to_idx = -1;
    for (size_t i = 0; i < train->stops.size(); ++i) {
        if (train->stops[i].station_id == from_station) from_idx = static_cast<int>(i);
        if (train->stops[i].station_id == to_station && from_idx >= 0) {
            to_idx = static_cast<int>(i);
            break;
        }
    }
    if (from_idx < 0 || to_idx < 0) {
        result.error = "Invalid from/to stations for this train";
        return result;
    }

    // 3. 预留座位（原子操作，由 SeatInventory 内部锁保证）
    auto reservation = SeatInventory::instance().reserve(train_id, date, seat_type, count);
    if (!reservation.success) {
        result.error = "Insufficient seats";
        return result;
    }

    // 4. 计算票价里程 = 列车经过站序列逐段 Haversine 累加（优先 route_stations）
    double trip_km = 0.0;
    auto& ds = DataStore::instance();
    // 使用 route_stations（含经过不停车的站），更精确
    std::vector<uint32_t> fallback;
    const std::vector<uint32_t>* ids = &train->route_stations;
    if (ids->empty()) {
        for (const auto& s : train->stops) fallback.push_back(s.station_id);
        ids = &fallback;
    }

    int r_from = -1, r_to = -1;
    for (size_t i = 0; i < ids->size(); ++i) {
        if ((*ids)[i] == from_station) r_from = static_cast<int>(i);
        if ((*ids)[i] == to_station && r_from >= 0) { r_to = static_cast<int>(i); break; }
    }
    if (r_from >= 0 && r_to > r_from) {
        for (int i = r_from; i < r_to; ++i) {
            auto* sa = ds.getStation((*ids)[i]);
            auto* sb = ds.getStation((*ids)[i + 1]);
            if (sa && sb) {
                trip_km += haversineDist(*sa, *sb);
            }
        }
    }

    const double BASE = 0.30;
    double mult = 1.0;
    switch (seat_type) {
        case SeatType::BUSINESS: mult = 3.0; break;
        case SeatType::FIRST:    mult = 2.0; break;
        case SeatType::SECOND:   mult = 1.0; break;
        case SeatType::HARD_SLEEPER: mult = 0.8; break;
        case SeatType::HARD_SEAT:    mult = 0.4; break;
        case SeatType::NO_SEAT:      mult = 0.3; break;
    }

    // 5. 创建订单
    Order order;
    order.id = makeUuid();
    order.user_id = user_id;
    order.train_id = train_id;
    order.date = date;
    order.from_station = from_station;
    order.to_station = to_station;
    order.seat_type = seat_type;
    order.seat_number = reservation.seat_numbers.empty() ? 0 : reservation.seat_numbers[0];
    order.price = trip_km * BASE * mult * count;
    order.status = OrderStatus::PAID;
    order.created_at = nowIso();
    order.passenger_name = passenger_name;
    order.passenger_id = passenger_id;

    orders_.push_back(order);
    Logger::instance().info("Order created: " + order.id + " for " + train_id);

    result.order = order;
    return result;
}

// ── 退票 ──

OrderService::RefundResult OrderService::refundOrder(const std::string& order_id,
                                                      const std::string& user_id) {
    RefundResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 找订单
    auto it = std::find_if(orders_.begin(), orders_.end(),
        [&](const Order& o) { return o.id == order_id; });
    if (it == orders_.end()) {
        result.error = "Order not found";
        return result;
    }

    // 2. 权限检查：只能退自己的
    if (it->user_id != user_id) {
        result.error = "Can only refund your own orders";
        return result;
    }

    if (it->status != OrderStatus::PAID) {
        result.error = "Order is not in PAID status";
        return result;
    }

    // 3. 计算退款金额（按退票时间阶梯费率）
    auto* train = DataStore::instance().getTrain(it->train_id);
    if (!train) {
        result.error = "Train not found";
        return result;
    }

    int departure_hhmm = 0;
    for (const auto& stop : train->stops) {
        if (stop.station_id == it->from_station) {
            departure_hhmm = stop.departure;
            break;
        }
    }

    double rate = calcRefund(it->price, departure_hhmm);
    if (rate <= 0.0) {
        result.error = "Train already departed, cannot refund";
        return result;
    }

    double refund = it->price * rate;

    // 4. 释放座位
    if (it->seat_number > 0) {
        SeatInventory::instance().release(it->train_id, it->date,
            it->seat_type, {it->seat_number});
    }

    // 5. 更新订单状态
    it->status = OrderStatus::REFUNDED;
    Logger::instance().info("Order refunded: " + order_id
        + " refund=" + std::to_string(refund) + " rate=" + std::to_string(rate * 100) + "%");

    result.refund_amount = refund;
    return result;
}

// ── 订单查询 ──

std::vector<Order> OrderService::getOrders(const std::string& user_id,
                                            std::optional<OrderStatus> status) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Order> result;
    for (const auto& order : orders_) {
        if (order.user_id != user_id) continue;
        if (status && order.status != *status) continue;
        result.push_back(order);
    }
    // 按创建时间倒序
    std::sort(result.begin(), result.end(),
        [](const Order& a, const Order& b) { return a.created_at > b.created_at; });
    return result;
}

const Order* OrderService::getOrder(const std::string& order_id) const {
    for (const auto& o : orders_) {
        if (o.id == order_id) return &o;
    }
    return nullptr;
}

// ── 退票费率 ──

double OrderService::calcRefund(double /*price*/, int departure_hhmm) const {
    int now = nowHHMM();
    int hours_before = (departure_hhmm / 100 * 60 + departure_hhmm % 100
                        - now / 100 * 60 - now % 100) / 60;

    if (hours_before < 0) return 0.0;    // 已发车
    if (hours_before < 2) return 0.80;   // 2 小时内
    if (hours_before < 24) return 0.90;  // 2-24 小时
    return 0.95;                          // 24 小时以上
}
