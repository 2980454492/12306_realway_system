// test_staff.cpp — 职工端测试（列车管理、冲突检测）
#include <gtest/gtest.h>
#include "staff/train_manager.h"
#include "data/data_store.h"

class StaffTest : public ::testing::Test {
protected:
    void SetUp() override {
        DataStore::instance().initialize();
        TrainManager::instance().initialize();
    }
};

TEST_F(StaffTest, ValidateGoodTrain) {
    Train t;
    t.id = "G9999";
    t.type = TrainType::REGULAR;
    t.valid_from = "2026-08-10";  // >3 days from now
    Stop s1, s2;
    s1.station_id = 1; s1.departure = 800; s1.line_id = 1;
    s2.station_id = 3; s2.arrival = 1000; s2.line_id = 1;
    t.stops = {s1, s2};

    auto vr = TrainManager::instance().validate(t, true);
    EXPECT_TRUE(vr.valid);
}

TEST_F(StaffTest, ValidateDuplicateTrainId) {
    // Find an ACTIVE train (not ARCHIVED — ARCHIVED IDs can be reused)
    auto& trains = DataStore::instance().getAllTrains();
    std::string dup_id;
    for (const auto& tr : trains) {
        if (tr.status == TrainStatus::ACTIVE) {
            dup_id = tr.id;
            break;
        }
    }
    if (dup_id.empty()) {
        GTEST_SKIP() << "No ACTIVE train available";
        return;
    }
    Train t;
    t.id = dup_id;
    t.valid_from = "2026-08-10";
    Stop s1, s2;
    s1.station_id = 1; s1.departure = 800;
    s2.station_id = 3; s2.arrival = 1000;
    t.stops = {s1, s2};

    auto vr = TrainManager::instance().validate(t, true);
    EXPECT_FALSE(vr.valid) << "Should reject duplicate ACTIVE train ID " << dup_id;
}

TEST_F(StaffTest, ValidateTooFewStops) {
    Train t;
    t.id = "G8888";
    t.valid_from = "2026-08-10";
    t.stops = {{}};  // only 1 stop

    auto vr = TrainManager::instance().validate(t, true);
    EXPECT_FALSE(vr.valid);
}

TEST_F(StaffTest, CheckTrainBundlesValidationAndConflicts) {
    Train t;
    t.id = "G7777";
    t.type = TrainType::REGULAR;
    t.valid_from = "2026-08-10";
    Stop s1, s2;
    s1.station_id = 1; s1.departure = 800; s1.line_id = 1;
    s2.station_id = 3; s2.arrival = 1000; s2.line_id = 1;
    t.stops = {s1, s2};

    auto cr = TrainManager::instance().checkTrain(t, true);
    EXPECT_TRUE(cr.valid) << cr.error;
}

TEST_F(StaffTest, ConflictDetection) {
    auto& trains = DataStore::instance().getAllTrains();
    if (trains.empty()) {
        GTEST_SKIP();
        return;
    }
    auto* existing = DataStore::instance().getTrain(trains[0].id);
    ASSERT_NE(existing, nullptr);
    auto conflicts = TrainManager::instance().detectConflicts(*existing);
    // Conflict detection should not crash
    SUCCEED();
}
