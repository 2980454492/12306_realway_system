// user_service.cpp — 用户管理实现（委托 AuthService）
#include "sys_admin/user_service.h"
#include "auth/auth_service.h"

namespace user_service {

std::optional<User> createUser(const std::string& username,
                               const std::string& password, UserRole role) {
    return AuthService::instance().createUser(username, password, role);
}

UpdateResult updateUser(const std::string& target_id,
                        const std::string& current_user_id,
                        std::optional<UserRole> role,
                        std::optional<bool> active,
                        const std::string& new_password) {
    auto r = AuthService::instance().updateUser(target_id, current_user_id, role, active, new_password);
    return {r.success, r.error};
}

DeleteResult deleteUser(const std::string& target_id,
                        const std::string& current_user_id) {
    auto r = AuthService::instance().deleteUser(target_id, current_user_id);
    return {r.success, r.error};
}

std::vector<User> getAllUsers() {
    return AuthService::instance().getAllUsers();
}

const User* findUser(const std::string& username) {
    return AuthService::instance().findUser(username);
}

const User* findUserById(const std::string& id) {
    return AuthService::instance().findUserById(id);
}

}  // namespace user_service
