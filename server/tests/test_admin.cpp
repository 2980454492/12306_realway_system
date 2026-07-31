// test_admin.cpp — 管理员测试（用户管理、审计、配置）
#include <gtest/gtest.h>
#include "auth/auth_service.h"
#include "sys_admin/audit_service.h"
#include "sys_admin/system_config.h"
#include "data/data_store.h"
#include <thread>
#include <chrono>

class AdminTest : public ::testing::Test {
protected:
    void SetUp() override {
        AuthService::instance().initialize();
        SystemConfig::instance().initialize("config/system.json");
    }

    void TearDown() override {
        AuditLogger::instance().shutdown();
    }
};

TEST_F(AdminTest, GetAllUsers) {
    auto users = AuthService::instance().getAllUsers();
    EXPECT_GT(users.size(), 4);  // at least 5 seed users
}

TEST_F(AdminTest, CreateAndDeleteUser) {
    auto user = AuthService::instance().createUser("test_admin_user", "pass123", UserRole::PASSENGER);
    ASSERT_TRUE(user.has_value());

    auto result = AuthService::instance().deleteUser(user->id, "sys_admin_id");
    EXPECT_TRUE(result.success);
}

TEST_F(AdminTest, CannotDeleteSelfUnlessSysAdmin) {
    auto* passenger = AuthService::instance().findUser("passenger");
    ASSERT_NE(passenger, nullptr);
    auto result = AuthService::instance().deleteUser(passenger->id, passenger->id);
    EXPECT_FALSE(result.success);
}

TEST_F(AdminTest, SystemConfigDefault) {
    auto& cfg = SystemConfig::instance();
    EXPECT_GT(cfg.ratePerKm("G", SeatType::SECOND), 0.0);
    EXPECT_GT(cfg.refundRate24h(), 0.0);
    EXPECT_LT(cfg.refundRate2h(), cfg.refundRate24h());
}

TEST_F(AdminTest, SystemConfigUpdate) {
    auto& cfg = SystemConfig::instance();
    double old = cfg.ratePerKm("G", SeatType::SECOND);
    cfg.setRate('G', SeatType::SECOND, 0.50);
    EXPECT_NEAR(cfg.ratePerKm("G", SeatType::SECOND), 0.50, 0.01);
    cfg.setRate('G', SeatType::SECOND, old);  // restore
}
