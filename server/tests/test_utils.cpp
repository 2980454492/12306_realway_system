// test_utils.cpp — 工具函数测试
#include <gtest/gtest.h>
#include "utils.h"

TEST(UtilsTest, GenerateUuid) {
    auto a = generateUuid();
    auto b = generateUuid();
    EXPECT_FALSE(a.empty());
    EXPECT_FALSE(b.empty());
    EXPECT_NE(a, b);
    EXPECT_EQ(a.size(), 36);  // UUID v4 标准长度
}

TEST(UtilsTest, HaversineDist) {
    Station a, b;
    a.latitude = 39.90; a.longitude = 116.40;  // 北京
    b.latitude = 31.23; b.longitude = 121.47;  // 上海
    double dist = haversineDist(a, b);
    EXPECT_GT(dist, 1000.0);
    EXPECT_LT(dist, 1200.0);  // 北京→上海约 1068km

    // 同一点距离为 0
    EXPECT_NEAR(haversineDist(a, a), 0.0, 0.01);
}

TEST(UtilsTest, TodayStr) {
    std::string today = todayStr();
    EXPECT_EQ(today.size(), 10);
    EXPECT_EQ(today[4], '-');
    EXPECT_EQ(today[7], '-');
}

TEST(UtilsTest, NowHHMM) {
    int t = nowHHMM();
    EXPECT_GE(t, 0);
    EXPECT_LE(t, 2359);
}

TEST(UtilsTest, TimeDiff) {
    EXPECT_EQ(timeDiff(800, 1000), 120);
    EXPECT_EQ(timeDiff(2300, 100), 120);  // 跨天
    EXPECT_EQ(timeDiff(-1, 1000), 9999); // invalid
}

TEST(UtilsTest, IsFuture) {
    std::string tomorrow = todayStr();
    // skip — depends on current date
}

TEST(UtilsTest, RoleToString) {
    EXPECT_EQ(roleToString(UserRole::PASSENGER), "PASSENGER");
    EXPECT_EQ(roleToString(UserRole::STAFF), "STAFF");
    EXPECT_EQ(roleToString(UserRole::APPROVER), "APPROVER");
    EXPECT_EQ(roleToString(UserRole::INFRA_ADMIN), "INFRA_ADMIN");
    EXPECT_EQ(roleToString(UserRole::SYS_ADMIN), "SYS_ADMIN");
}

TEST(UtilsTest, RoleFromString) {
    EXPECT_EQ(roleFromString("PASSENGER"), UserRole::PASSENGER);
    EXPECT_EQ(roleFromString("STAFF"), UserRole::STAFF);
    EXPECT_EQ(roleFromString("SYS_ADMIN"), UserRole::SYS_ADMIN);
    EXPECT_EQ(roleFromString("INVALID"), UserRole::PASSENGER);  // fallback
}
