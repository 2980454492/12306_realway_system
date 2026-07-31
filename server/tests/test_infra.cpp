// test_infra.cpp — 基础设施管理测试（站点/线路 CRUD）
#include <gtest/gtest.h>
#include "data/data_store.h"
#include "infra_admin/station_service.h"
#include "infra_admin/line_service.h"
#include "data/railway_graph.h"

class InfraTest : public ::testing::Test {
protected:
    void SetUp() override {
        DataStore::instance().initialize();
    }
};

TEST_F(InfraTest, StationServiceGetAll) {
    auto stations = station_service::getAll();
    EXPECT_GT(stations.size(), 300);
}

TEST_F(InfraTest, StationServiceCRUD) {
    Station s;
    s.name = "测试站点";
    s.city = "测试市";
    s.type = StationType::NORMAL;
    s.latitude = 30.0;
    s.longitude = 110.0;

    auto added = station_service::add(s);
    EXPECT_GT(added.id, 0);

    added.name = "测试站点（已更名）";
    EXPECT_TRUE(station_service::update(added.id, added));

    EXPECT_TRUE(station_service::remove(added.id));
}

TEST_F(InfraTest, LineServiceGetAll) {
    auto lines = line_service::getAll();
    EXPECT_GT(lines.size(), 25);
}

TEST_F(InfraTest, LineServiceCRUD) {
    Line l;
    l.name = "测试线路";
    l.type = LineType::EXPRESS;
    l.stations = {"北京", "天津"};
    l.max_speed_kmh = 200;

    auto added = line_service::add(l);
    EXPECT_GT(added.id, 0);

    added.max_speed_kmh = 180;
    EXPECT_TRUE(line_service::update(added.id, added));

    EXPECT_TRUE(line_service::remove(added.id));
}

TEST_F(InfraTest, RailwayGraphBuild) {
    RailwayGraph graph;
    graph.build(DataStore::instance().getAllLines());
    // Graph built successfully — no crash
    SUCCEED();
}

TEST_F(InfraTest, NewLineTypeExpress) {
    Line l;
    l.name = "快速铁路测试";
    l.type = LineType::EXPRESS;
    l.stations = {"北京", "上海"};
    l.max_speed_kmh = 200;

    auto added = line_service::add(l);
    EXPECT_EQ(added.type, LineType::EXPRESS);
    line_service::remove(added.id);
}
