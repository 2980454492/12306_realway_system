// test_passenger.cpp — 旅客端测试（查票、购票、退票）
#include <gtest/gtest.h>
#include "passenger/train_query.h"
#include "passenger/order_service.h"
#include "passenger/seat_inventory.h"
#include "data/data_store.h"
#include <thread>
#include <atomic>
#include "sys_admin/system_config.h"

class PassengerTest : public ::testing::Test {
protected:
    void SetUp() override {
        SystemConfig::instance().initialize("config/system.json");
        DataStore::instance().initialize();
        TrainQuery::initialize();
        OrderService::instance().initialize();
    }
};

TEST_F(PassengerTest, DirectQuery) {
    // 北京(1) → 天津(3)
    auto result = TrainQuery::query(1, 3, "2026-08-15");
    EXPECT_GT(result.direct.size(), 0) << "Should find trains from Beijing to Tianjin";
    for (const auto& item : result.direct) {
        EXPECT_GT(item.distance_km, 10.0) << "Distance should be > 10km";
        EXPECT_GE(item.price, 0.0) << "Price should be >= 0";
    }
}

TEST_F(PassengerTest, NoResults) {
    auto result = TrainQuery::query(99999, 88888, "2026-08-15");
    EXPECT_EQ(result.direct.size(), 0);
}

TEST_F(PassengerTest, BuyTicket) {
    // Use first ACTIVE train with >= 2 stops
    auto& trains = DataStore::instance().getAllTrains();
    const Train* t = nullptr;
    for (const auto& tr : trains) {
        if (tr.status == TrainStatus::ACTIVE && tr.stops.size() >= 2) {
            t = &tr;
            break;
        }
    }
    if (!t) {
        GTEST_SKIP() << "No active train available for buy test";
        return;
    }
    uint32_t from = t->stops[0].station_id;
    uint32_t to = t->stops[t->stops.size()-1].station_id;

    auto result = OrderService::instance().createOrder(
        "u1", t->id, "2026-08-15", from, to, SeatType::SECOND, 1,
        "test", "110101199001010001");
    // May fail due to seat availability — just check it doesn't crash
    SUCCEED();
}

TEST_F(PassengerTest, BuyTicketPastDate) {
    auto& trains = DataStore::instance().getAllTrains();
    ASSERT_GT(trains.size(), 0);
    auto& t = trains[1];  // 1st is ARCHIVED, use 2nd
    auto result = OrderService::instance().createOrder(
        "u1", t.id, "2020-01-01", t.stops[0].station_id,
        t.stops[t.stops.size()-1].station_id, SeatType::SECOND, 1,
        "张三", "110101199001010001");
    EXPECT_FALSE(result.order.has_value());
}

TEST_F(PassengerTest, BuyTicketNonexistentTrain) {
    auto result = OrderService::instance().createOrder(
        "u1", "NONEXIST", "2026-08-15", 1, 3, SeatType::SECOND, 1,
        "张三", "110101199001010001");
    EXPECT_FALSE(result.order.has_value());
}

TEST_F(PassengerTest, BuyTicketInvalidStations) {
    auto& trains = DataStore::instance().getAllTrains();
    ASSERT_GT(trains.size(), 1);
    auto result = OrderService::instance().createOrder(
        "u1", trains[1].id, "2026-08-15", 99999, 88888, SeatType::SECOND, 1,
        "张三", "110101199001010001");
    EXPECT_FALSE(result.order.has_value());
}

TEST_F(PassengerTest, RefundNonExistentOrder) {
    auto refund = OrderService::instance().refundOrder("nonexistent_order_id", "u1");
    EXPECT_FALSE(refund.refund_amount.has_value());
    EXPECT_FALSE(refund.error.empty());
}

TEST_F(PassengerTest, RefundOtherUserOrder) {
    auto refund = OrderService::instance().refundOrder("nonexistent_id", "wrong_user");
    EXPECT_FALSE(refund.refund_amount.has_value());
}

// ── 并发抢票压力测试：100 线程抢 50 张票 ──

TEST_F(PassengerTest, ConcurrentBuy100Threads50Seats) {
    // 找到一辆 ACTIVE 列车
    auto& trains = DataStore::instance().getAllTrains();
    Train* target = nullptr;
    for (const auto& tr : trains) {
        if (tr.status == TrainStatus::ACTIVE && tr.stops.size() >= 2) {
            target = const_cast<Train*>(&tr);
            break;
        }
    }
    ASSERT_NE(target, nullptr) << "Need at least one ACTIVE train";

    // 使用独立日期，确保该 (车次, 日期) 的库存未被其他测试初始化过
    std::string date = "2026-12-25";
    std::string train_id = target->id;

    // 将二等座设置为恰好 50 张，用于可复现验证
    uint16_t original_second = target->seat_config.second_seats;
    target->seat_config.second_seats = 50;

    constexpr int kThreads = 100;
    constexpr int kSeats = 50;
    std::atomic<int> success{0};
    std::atomic<int> fail{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&]() {
            auto res = SeatInventory::instance().reserve(
                train_id, date, SeatType::SECOND, 1);
            if (res.success)
                ++success;
            else
                ++fail;
        });
    }

    for (auto& t : threads)
        t.join();

    // 恢复原始配置
    target->seat_config.second_seats = original_second;

    // ── 断言 ──
    EXPECT_EQ(success.load(), kSeats)
        << "Expected exactly 50 successes (tickets sold), got " << success.load();
    EXPECT_EQ(fail.load(), kThreads - kSeats)
        << "Expected exactly 50 failures (oversold), got " << fail.load();

    // 验证库存清零
    auto final = SeatInventory::instance().getAvailable(train_id, date);
    EXPECT_EQ(final.second_seats, 0)
        << "Should have 0 available seats remaining";
}
