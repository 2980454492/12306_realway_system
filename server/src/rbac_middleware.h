// rbac_middleware.h — RBAC 权限中间件，JWT 校验 + 角色-权限映射
#pragma once

#include <string>
#include <bitset>
#include <unordered_map>
#include <optional>
#include <cstdint>

// ── 权限枚举（bit 索引，对应 bitset<64> 的位置）──

enum class Permission : size_t {
    QUERY_TRAINS    = 0,   // 查票
    BUY_TICKETS     = 1,   // 购票
    REFUND_OWN      = 2,   // 退票（自己的订单）
    VIEW_OWN_ORDERS = 3,   // 查看订单
    MANAGE_TRAINS   = 4,   // 增删改列车
    MANAGE_STATIONS = 5,   // 站点管理
    MANAGE_LINES    = 6,   // 线路管理
    APPROVE         = 7,   // 审批
    MANAGE_USERS    = 8,   // 用户管理
    VIEW_AUDIT      = 9,   // 查看审计日志
    SYSTEM_CONFIG   = 10,  // 系统配置
};

using PermissionSet = std::bitset<64>;

// ── 认证上下文 ──

struct AuthContext {
    std::string user_id;
    std::string role;
    PermissionSet permissions;

    /** 检查是否拥有某项权限 */
    bool hasPermission(Permission p) const {
        return permissions.test(static_cast<size_t>(p));
    }
};

// ── RbacMiddleware ──

/**
 * RbacMiddleware — 权限中间件。
 * 1. 从 Authorization header 提取 Bearer token
 * 2. 校验 JWT 签名和过期
 * 3. 根据 role 构建 PermissionSet
 * 4. 为特定路由检查所需权限
 */
class RbacMiddleware {
public:
    /** 初始化角色-权限映射 */
    static void initialize();

    /** 从 Authorization header 解析并校验 JWT。
     *  成功返回 AuthContext，失败返回 nullopt。 */
    static std::optional<AuthContext> authenticate(const std::string& auth_header);

    /** 检查 AuthContext 是否拥有所需权限 */
    static bool authorize(const AuthContext& ctx, Permission required);

private:
    // 角色 → 权限位图
    static std::unordered_map<std::string, PermissionSet> role_permissions_;
    static bool initialized_;
};
