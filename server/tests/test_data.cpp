// test_data.cpp — 数据层测试
#include <gtest/gtest.h>
#include "data/data_store.h"

class DataTest : public ::testing::Test {
protected:
    void SetUp() override {
        DataStore::instance().initialize();
    }
};

TEST_F(DataTest, StationsLoaded) {
    auto& stations = DataStore::instance().getAllStations();
    EXPECT_GT(stations.size(), 300);  // 全国地级市
    // 验证关键站
    auto* bj = DataStore::instance().getStation(1);
    ASSERT_NE(bj, nullptr);
    EXPECT_EQ(bj->city, "北京");
}

TEST_F(DataTest, LinesLoaded) {
    auto& lines = DataStore::instance().getAllLines();
    EXPECT_GT(lines.size(), 25);  // 全国干线
    // 验证线路使用城市名
    for (const auto& line : lines) {
        EXPECT_FALSE(line.name.empty());
        EXPECT_GE(line.stations.size(), 2);
    }
}

TEST_F(DataTest, TrainsLoaded) {
    auto& trains = DataStore::instance().getAllTrains();
    EXPECT_GT(trains.size(), 50);
}

TEST_F(DataTest, GetTrain) {
    auto& trains = DataStore::instance().getAllTrains();
    ASSERT_GT(trains.size(), 0);
    auto* train = DataStore::instance().getTrain(trains[0].id);
    ASSERT_NE(train, nullptr);
    EXPECT_GE(train->stops.size(), 2);
}

TEST_F(DataTest, StationCRUD) {
    Station s;
    s.name = "测试站";
    s.city = "测试市";
    s.type = StationType::HIGH_SPEED;
    s.latitude = 35.0;
    s.longitude = 110.0;

    EXPECT_TRUE(DataStore::instance().addStation(s));
    EXPECT_GT(s.id, 0);

    // Update
    s.name = "测试站(已改名)";
    EXPECT_TRUE(DataStore::instance().updateStation(s.id, s));

    // Delete
    EXPECT_TRUE(DataStore::instance().removeStation(s.id));
}

TEST_F(DataTest, LineCRUD) {
    Line l;
    l.name = "测试线路";
    l.type = LineType::HIGH_SPEED;
    l.stations = {"北京", "上海"};
    l.max_speed_kmh = 350;

    EXPECT_TRUE(DataStore::instance().addLine(l));
    EXPECT_GT(l.id, 0);

    // Update
    l.max_speed_kmh = 300;
    EXPECT_TRUE(DataStore::instance().updateLine(l.id, l));

    // Delete
    EXPECT_TRUE(DataStore::instance().removeLine(l.id));
}
