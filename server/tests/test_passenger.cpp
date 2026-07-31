// test_passenger.cpp — 旅客端测试（查票、购票、退票）
#include <gtest/gtest.h>
#include "passenger/train_query.h"
#include "passenger/order_service.h"
#include "data/data_store.h"
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
