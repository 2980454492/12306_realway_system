// rbac_middleware.cpp — RBAC 中间件实现
#include "auth/rbac_middleware.h"
#include "auth/jwt_service.h"
#include "core/logger.h"

std::unordered_map<std::string, PermissionSet> RbacMiddleware::role_permissions_;
bool RbacMiddleware::initialized_ = false;

void RbacMiddleware::initialize() {
    if (initialized_) return;

    // ── 旅客权限 ──
    PermissionSet passenger;
    passenger.set(static_cast<size_t>(Permission::QUERY_TRAINS));
    passenger.set(static_cast<size_t>(Permission::BUY_TICKETS));
    passenger.set(static_cast<size_t>(Permission::REFUND_OWN));
    passenger.set(static_cast<size_t>(Permission::VIEW_OWN_ORDERS));
    role_permissions_["PASSENGER"] = passenger;

    // ── 铁路职工（普通员工）— 增删改列车，不可审批 ──
    PermissionSet staff = passenger;
    staff.set(static_cast<size_t>(Permission::MANAGE_TRAINS));
    staff.set(static_cast<size_t>(Permission::MANAGE_STATIONS));
    staff.set(static_cast<size_t>(Permission::MANAGE_LINES));
    role_permissions_["STAFF"] = staff;

    // ── 审批职工 — 审批通过/驳回，不可增删改列车 ──
    PermissionSet approver = passenger;
    approver.set(static_cast<size_t>(Permission::APPROVE));
    role_permissions_["APPROVER"] = approver;

    // ── 管理员 — 继承职工 + 审批职工 + 管理权限 ──
    PermissionSet admin = staff;
    admin.set(static_cast<size_t>(Permission::APPROVE));
    admin.set(static_cast<size_t>(Permission::MANAGE_USERS));
    admin.set(static_cast<size_t>(Permission::VIEW_AUDIT));
    admin.set(static_cast<size_t>(Permission::SYSTEM_CONFIG));
    role_permissions_["ADMIN"] = admin;

    initialized_ = true;
    Logger::instance().info("RBAC middleware initialized: 4 roles, 11 permissions");
}

std::optional<AuthContext> RbacMiddleware::authenticate(const std::string& auth_header) {
    // 期望格式：Bearer <token>
    const std::string prefix = "Bearer ";
    if (auth_header.size() < prefix.size() ||
        auth_header.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }

    std::string token = auth_header.substr(prefix.size());
    auto payload = JwtService::instance().verifyToken(token);
    if (!payload)
        return std::nullopt;  // 签名错误或已过期

    AuthContext ctx;
    ctx.user_id = payload->user_id;
    ctx.role = payload->role;

    // 根据角色填充权限位图
    auto it = role_permissions_.find(payload->role);
    if (it != role_permissions_.end()) {
        ctx.permissions = it->second;
    }

    return ctx;
}

bool RbacMiddleware::authorize(const AuthContext& ctx, Permission required) {
    return ctx.hasPermission(required);
}
