// order_service.cpp — OrderService 实现
#include "passenger/order_service.h"
#include "passenger/seat_inventory.h"
#include "data/data_store.h"
#include "core/utils.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace {
    // ── 当前时间 ISO 8601 ──
    std::string nowIso() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

}

// ── 单例 ──

OrderService& OrderService::instance() {
    static OrderService svc;
    return svc;
}

// ── 持久化 ──

bool OrderService::initialize(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_dir_ = data_dir;

    std::string path = data_dir + "/orders.json";
    if (!fs::exists(path)) {
        Logger::instance().info("No existing orders file, starting fresh");
        return true;
    }

    try {
        std::ifstream in(path);
        using json = nlohmann::json;
        json j;
        in >> j;
        orders_ = j.get<std::vector<Order>>();
        Logger::instance().info("Loaded " + std::to_string(orders_.size()) + " orders from file");

        // 恢复已支付订单的座位占用
        int restored = 0;
        for (const auto& order : orders_) {
            if (order.status == OrderStatus::PAID && order.seat_number > 0) {
                SeatInventory::instance().markSold(
                    order.train_id, order.date, order.seat_type, order.seat_number);
                restored++;
            }
        }
        if (restored > 0) {
            Logger::instance().info("Restored " + std::to_string(restored) + " seat reservations");
        }
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to load orders: ") + e.what());
        return false;
    }
}

void OrderService::saveOrders() const {
    // 调用方已持有 mutex_
    std::string path = data_dir_ + "/orders.json";
    try {
        using json = nlohmann::json;
        json j = orders_;
        std::ofstream out(path);
        out << j.dump(2);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save orders: ") + e.what());
    }
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

    // 0. 日期不能是过去
    if (!isTodayOrFuture(date, 14)) {
        result.error = "Date must be within 14 days from today";
        return result;
    }

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

    // 3. 仅当乘车日期为今天时校验是否已发车
    int departure_hhmm = train->stops[from_idx].departure;
    if (departure_hhmm > 0 && isToday(date) && nowHHMM() > departure_hhmm) {
        result.error = "Train already departed";
        return result;
    }

    // 4. 预留座位（原子操作，由 SeatInventory 内部锁保证）
    auto reservation = SeatInventory::instance().reserve(train_id, date, seat_type, count);
    if (!reservation.success) {
        result.error = "Insufficient seats";
        return result;
    }

    // 5. 计算票价里程（复用共享函数）
    double trip_km = calcRouteDistance(*train, from_station, to_station, DataStore::instance());

    // 6. 创建订单
    Order order;
    order.id = generateUuid();
    order.user_id = user_id;
    order.train_id = train_id;
    order.date = date;
    order.from_station = from_station;
    order.to_station = to_station;
    order.seat_type = seat_type;
    order.seat_number = reservation.seat_numbers.empty() ? 0 : reservation.seat_numbers[0];
    order.price = trip_km * BASE_RATE_PER_KM * seatPriceMultiplier(seat_type) * count;
    order.status = OrderStatus::PAID;
    order.created_at = nowIso();
    order.passenger_name = passenger_name;
    order.passenger_id = passenger_id;

    orders_.push_back(order);
    saveOrders();
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

    double rate = calcRefund(it->date, departure_hhmm);
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
    saveOrders();
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

double OrderService::calcRefund(const std::string& date, int departure_hhmm) const {
    // 非今天的票，距发车 >24 小时，最高费率
    if (!isToday(date)) return 0.95;

    // 今天的票，按时距计算
    int minutes_before = timeDiff(nowHHMM(), departure_hhmm);

    if (minutes_before < 0) return 0.0;          // 已发车
    if (minutes_before < 120) return 0.80;        // 2 小时内
    if (minutes_before < 1440) return 0.90;       // 2-24 小时
    return 0.95;                                   // 24 小时以上
}
