// order_service.h — 订单服务，购票/退票/查询
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <optional>
#include <mutex>

/**
 * OrderService 单例 — 管理订单生命周期。
 * 购票原子性由 SeatInventory 保证。
 * 退票费率：发车前 >24h 退 95%，2-24h 退 90%，<2h 退 80%，发车后不可退。
 */
class OrderService {
public:
    static OrderService& instance();

    OrderService(const OrderService&) = delete;
    OrderService& operator=(const OrderService&) = delete;

    /** 从目录加载订单文件，持久化路径为 data_dir/orders.json */
    bool initialize(const std::string& data_dir);

    /**
     * 购票请求。
     * @return 成功返回订单，失败返回 error 信息
     */
    struct OrderResult {
        std::optional<Order> order;
        std::string error;  // 失败时的原因
    };
    OrderResult createOrder(const std::string& user_id,
                            const std::string& train_id,
                            const std::string& date,
                            uint32_t from_station,
                            uint32_t to_station,
                            SeatType seat_type,
                            int count,
                            const std::string& passenger_name,
                            const std::string& passenger_id);

    /**
     * 退票。
     * @return 成功返回退款金额，失败返回 error
     */
    struct RefundResult {
        std::optional<double> refund_amount;
        std::string error;
    };
    RefundResult refundOrder(const std::string& order_id, const std::string& user_id);

    /** 查询用户的所有订单，按创建时间倒序，可选状态筛选 */
    std::vector<Order> getOrders(const std::string& user_id,
                                 std::optional<OrderStatus> status = std::nullopt) const;

    /** 按 ID 查订单 */
    const Order* getOrder(const std::string& order_id) const;

private:
    OrderService() = default;

    void saveOrders() const;

    std::vector<Order> orders_;
    mutable std::mutex mutex_;
    std::string data_dir_;
};
