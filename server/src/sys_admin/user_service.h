// user_service.h — 用户管理服务（CRUD），供 SYS_ADMIN 路由调用
#pragma once

#include "models.h"
#include <string>
#include <vector>
#include <optional>

/** UserService 封装 AuthService 中的用户管理操作 */
namespace user_service {

std::optional<User> createUser(const std::string& username,
                               const std::string& password, UserRole role);

struct UpdateResult { bool success = false; std::string error; };
UpdateResult updateUser(const std::string& target_id,
                        const std::string& current_user_id,
                        std::optional<UserRole> role,
                        std::optional<bool> active,
                        const std::string& new_password);

struct DeleteResult { bool success = false; std::string error; };
DeleteResult deleteUser(const std::string& target_id,
                        const std::string& current_user_id);

std::vector<User> getAllUsers();
const User* findUser(const std::string& username);
const User* findUserById(const std::string& id);

}  // namespace user_service
