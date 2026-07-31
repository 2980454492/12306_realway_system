// test_approver.cpp — 审批流测试
#include <gtest/gtest.h>
#include "approver/approval_service.h"

class ApproverTest : public ::testing::Test {
protected:
    void SetUp() override {
        ApprovalService::instance().initialize();
    }
};

TEST_F(ApproverTest, SubmitApproval) {
    std::string id = ApprovalService::instance().submit(
        ApprovalType::CREATE_TRAIN, "staff_user", R"({"id":"G_TEST"})");
    EXPECT_FALSE(id.empty());

    auto* a = ApprovalService::instance().getApproval(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->status, ApprovalState::SUBMITTED);
    EXPECT_EQ(a->submitter_id, "staff_user");
}

TEST_F(ApproverTest, ApproveNonExistent) {
    auto result = ApprovalService::instance().approve("nonexistent", "approver_user");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(ApproverTest, FourEyesPrinciple) {
    std::string id = ApprovalService::instance().submit(
        ApprovalType::CREATE_TRAIN, "staff1", R"({"id":"G_TEST2"})");
    // Same person tries to approve
    auto result = ApprovalService::instance().approve(id, "staff1");
    EXPECT_FALSE(result.success);
}

TEST_F(ApproverTest, RejectApproval) {
    std::string id = ApprovalService::instance().submit(
        ApprovalType::DELETE_TRAIN, "staff2", R"({"id":"G_TEST3"})");
    auto result = ApprovalService::instance().reject(id, "approver1", "测试驳回");
    EXPECT_TRUE(result.success);

    auto* a = ApprovalService::instance().getApproval(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->status, ApprovalState::REJECTED);
    EXPECT_EQ(a->comment, "测试驳回");
}

TEST_F(ApproverTest, WithdrawApproval) {
    std::string id = ApprovalService::instance().submit(
        ApprovalType::CREATE_TRAIN, "staff3", R"({"id":"G_TEST4"})");
    auto result = ApprovalService::instance().withdraw(id, "staff3");
    EXPECT_TRUE(result.success);

    auto* a = ApprovalService::instance().getApproval(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->status, ApprovalState::WITHDRAWN);
}

TEST_F(ApproverTest, WithdrawByOther) {
    std::string id = ApprovalService::instance().submit(
        ApprovalType::CREATE_TRAIN, "staff4", R"({"id":"G_TEST5"})");
    auto result = ApprovalService::instance().withdraw(id, "other_user");
    EXPECT_FALSE(result.success);
}

TEST_F(ApproverTest, GetApprovalsFiltered) {
    auto all = ApprovalService::instance().getApprovals();
    EXPECT_GT(all.size(), 0);  // at least the ones we just created
}
