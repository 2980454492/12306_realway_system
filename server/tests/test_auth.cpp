// test_auth.cpp — 认证服务测试
#include <gtest/gtest.h>
#include "auth/auth_service.h"
#include "security/crypto.h"
#include "utils.h"

class AuthTest : public ::testing::Test {
protected:
    void SetUp() override {
        // AuthService::initialize() creates seed users if no users.json
        AuthService::instance().initialize();
    }
};

TEST_F(AuthTest, SeedUsersExist) {
    auto* admin = AuthService::instance().findUser("sys_admin");
    auto* infra = AuthService::instance().findUser("infra_admin");
    auto* staff = AuthService::instance().findUser("staff");
    auto* approver = AuthService::instance().findUser("approver");
    auto* passenger = AuthService::instance().findUser("passenger");

    EXPECT_NE(admin, nullptr);
    EXPECT_NE(infra, nullptr);
    EXPECT_NE(staff, nullptr);
    EXPECT_NE(approver, nullptr);
    EXPECT_NE(passenger, nullptr);
}

TEST_F(AuthTest, LoginSuccess) {
    auto user = AuthService::instance().verifyUser("sys_admin", "sys123");
    EXPECT_TRUE(user.has_value());
    EXPECT_EQ(user->role, UserRole::SYS_ADMIN);
    EXPECT_TRUE(user->active);
}

TEST_F(AuthTest, LoginFailWrongPassword) {
    auto user = AuthService::instance().verifyUser("staff", "wrongpassword");
    EXPECT_FALSE(user.has_value());
}

TEST_F(AuthTest, LoginFailNonexistent) {
    auto user = AuthService::instance().verifyUser("nonexistent", "pass");
    EXPECT_FALSE(user.has_value());
}

TEST_F(AuthTest, CreateUser) {
    std::string unique = "ut_" + generateUuid().substr(0, 8);
    auto* existing = AuthService::instance().findUser(unique);
    if (existing) {
        // Clean up from previous failed run
        const_cast<std::string&>(existing->username) = unique + "_old";
    }
    auto user = AuthService::instance().createUser(unique, "test123", UserRole::PASSENGER);
    ASSERT_TRUE(user.has_value()) << "Failed to create: " << unique;
    EXPECT_EQ(user->username, "testuser");
    EXPECT_EQ(user->role, UserRole::PASSENGER);
    EXPECT_TRUE(user->active);
    EXPECT_EQ(user->failed_attempts, 0);

    // Login with new user
    auto login = AuthService::instance().verifyUser("testuser", "test123");
    EXPECT_TRUE(login.has_value());
}

TEST_F(AuthTest, CreateDuplicateUser) {
    auto user = AuthService::instance().createUser("staff", "pass", UserRole::STAFF);
    EXPECT_FALSE(user.has_value());
}

TEST_F(AuthTest, UpdateUserRole) {
    auto result = AuthService::instance().updateUser(
        "nonexistent", "", UserRole::STAFF, {}, "");
    EXPECT_FALSE(result.success);
}

TEST_F(AuthTest, DeleteLastSysAdmin) {
    // Find sys_admin id
    auto* admin = AuthService::instance().findUser("sys_admin");
    ASSERT_NE(admin, nullptr);
    // Try to delete the only sys_admin
    auto result = AuthService::instance().deleteUser(admin->id, "other");
    EXPECT_FALSE(result.success);
}

TEST_F(AuthTest, AccountLockout) {
    // Reset lockout state from previous runs
    auto* p = const_cast<User*>(AuthService::instance().findUser("passenger"));
    ASSERT_NE(p, nullptr);
    p->failed_attempts = 0;
    p->locked_until.clear();

    // 5 wrong passwords → lockout
    for (int i = 0; i < 5; i++)
        AuthService::instance().verifyUser("passenger", "wrong");
    auto user = AuthService::instance().findUser("passenger");
    ASSERT_NE(user, nullptr);
    EXPECT_GE(user->failed_attempts, 5);
    EXPECT_FALSE(user->locked_until.empty());

    // Even correct password fails when locked
    auto login = AuthService::instance().verifyUser("passenger", "pass123");
    EXPECT_FALSE(login.has_value());
}
