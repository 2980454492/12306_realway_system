// test_rbac.cpp — RBAC 权限测试
#include <gtest/gtest.h>
#include "auth/rbac_middleware.h"
#include "auth/jwt_service.h"

class RbacTest : public ::testing::Test {
protected:
    void SetUp() override {
        JwtService::instance().initialize();
        RbacMiddleware::initialize();
    }
};

TEST_F(RbacTest, PassengerPermissions) {
    std::string token = JwtService::instance().generateToken("u1", "PASSENGER", 1800);
    auto ctx = RbacMiddleware::authenticate("Bearer " + token);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->hasPermission(Permission::QUERY_TRAINS));
    EXPECT_TRUE(ctx->hasPermission(Permission::BUY_TICKETS));
    EXPECT_TRUE(ctx->hasPermission(Permission::REFUND_OWN));
    EXPECT_FALSE(ctx->hasPermission(Permission::MANAGE_TRAINS));
    EXPECT_FALSE(ctx->hasPermission(Permission::APPROVE));
    EXPECT_FALSE(ctx->hasPermission(Permission::MANAGE_USERS));
}

TEST_F(RbacTest, StaffPermissions) {
    std::string token = JwtService::instance().generateToken("u2", "STAFF", 1800);
    auto ctx = RbacMiddleware::authenticate("Bearer " + token);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->hasPermission(Permission::QUERY_TRAINS));
    EXPECT_TRUE(ctx->hasPermission(Permission::MANAGE_TRAINS));
    EXPECT_FALSE(ctx->hasPermission(Permission::APPROVE));
    EXPECT_FALSE(ctx->hasPermission(Permission::MANAGE_USERS));
}

TEST_F(RbacTest, ApproverPermissions) {
    std::string token = JwtService::instance().generateToken("u3", "APPROVER", 1800);
    auto ctx = RbacMiddleware::authenticate("Bearer " + token);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->hasPermission(Permission::APPROVE));
    EXPECT_FALSE(ctx->hasPermission(Permission::MANAGE_TRAINS));
}

TEST_F(RbacTest, InfraAdminPermissions) {
    std::string token = JwtService::instance().generateToken("u4", "INFRA_ADMIN", 1800);
    auto ctx = RbacMiddleware::authenticate("Bearer " + token);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->hasPermission(Permission::MANAGE_STATIONS));
    EXPECT_TRUE(ctx->hasPermission(Permission::MANAGE_LINES));
    EXPECT_FALSE(ctx->hasPermission(Permission::MANAGE_USERS));
}

TEST_F(RbacTest, SysAdminPermissions) {
    std::string token = JwtService::instance().generateToken("u5", "SYS_ADMIN", 1800);
    auto ctx = RbacMiddleware::authenticate("Bearer " + token);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->hasPermission(Permission::MANAGE_USERS));
    EXPECT_TRUE(ctx->hasPermission(Permission::VIEW_AUDIT));
    EXPECT_TRUE(ctx->hasPermission(Permission::SYSTEM_CONFIG));
}

TEST_F(RbacTest, InvalidToken) {
    auto ctx = RbacMiddleware::authenticate("Bearer invalid_token");
    EXPECT_FALSE(ctx.has_value());
}

TEST_F(RbacTest, NoBearer) {
    auto ctx = RbacMiddleware::authenticate("invalid_token");
    EXPECT_FALSE(ctx.has_value());
}
