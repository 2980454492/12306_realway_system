# CMakeLists.txt — 12306 铁路票务系统构建配置
cmake_minimum_required(VERSION 3.16)
project(railway_server VERSION 0.1.0 LANGUAGES CXX)

# ── C++ 标准 ──
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 测试 ──
enable_testing()

# ── 编译选项 ──
# 面试展示用 Debug 模式，确保 sanitizer 可用
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Debug 模式下启用 sanitizer（可选，通过 -DENABLE_SANITIZERS=ON 开启）
option(ENABLE_SANITIZERS "Enable Address and Thread sanitizers" OFF)
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fsanitize=thread)
endif()

# ── 第三方依赖 ──
# nlohmann/json — header-only JSON 库
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# libsodium — argon2id 密码哈希、AES-256-GCM 加密
# 本地构建用 find_library，交叉编译用 SODIUM_LIB_PATH
option(SODIUM_LIB_PATH "Path to pre-compiled libsodium.a (for cross-compilation)" "")
option(SODIUM_INCLUDE_PATH "Path to libsodium include dir (for cross-compilation)" "")

if(SODIUM_LIB_PATH AND SODIUM_INCLUDE_PATH)
    # 交叉编译：使用预编译的静态库
    add_library(sodium STATIC IMPORTED)
    set_target_properties(sodium PROPERTIES
        IMPORTED_LOCATION "${SODIUM_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_PATH}"
    )
else()
    find_library(SODIUM_LIB sodium REQUIRED)
endif()

# ── 可执行目标 ──
set(SOURCES
    src/main.cpp
    src/system/logger.cpp
    src/http/server.cpp
    src/http/router.cpp
    src/http/router_auth.cpp
    src/http/router_passenger.cpp
    src/http/router_staff.cpp
    src/http/router_approver.cpp
    src/http/router_sys_admin.cpp
    src/http/router_infra_admin.cpp
    src/system/wal.cpp
    src/sys_admin/audit_service.cpp
    src/sys_admin/system_config.cpp
    src/sys_admin/user_service.cpp
    src/infra_admin/station_service.cpp
    src/infra_admin/line_service.cpp
    src/security/rate_limiter.cpp
    src/security/crypto.cpp
    src/data/data_store.cpp
    src/auth/auth_service.cpp
    src/auth/jwt_service.cpp
    src/auth/rbac_middleware.cpp
    src/passenger/train_query.cpp
    src/passenger/seat_inventory.cpp
    src/passenger/order_service.cpp
    src/staff/train_manager.cpp
    src/approver/approval_service.cpp
)

set(HEADERS
    src/models.h
    src/config.h
    src/utils.h
    src/system/logger.h
    src/http/server.h
    src/http/router.h
    src/http/router_helpers.h
    src/system/wal.h
    src/sys_admin/audit_service.h
    src/sys_admin/system_config.h
    src/sys_admin/user_service.h
    src/infra_admin/station_service.h
    src/infra_admin/line_service.h
    src/security/rate_limiter.h
    src/security/crypto.h
    src/data/data_store.h
    src/auth/auth_service.h
    src/auth/jwt_service.h
    src/auth/rbac_middleware.h
    src/passenger/train_query.h
    src/passenger/seat_inventory.h
    src/passenger/order_service.h
    src/staff/train_manager.h
    src/approver/approval_service.h
    vendor/httplib.h
)

add_executable(railway_server ${SOURCES} ${HEADERS})

# ── Include 路径 ──
target_include_directories(railway_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
if(SODIUM_INCLUDE_PATH)
    target_include_directories(railway_server PRIVATE ${SODIUM_INCLUDE_PATH})
endif()

# ── 链接库 ──
# nlohmann/json (header-only, 但 CMake 包提供 target)
if(SODIUM_LIB_PATH)
    target_link_libraries(railway_server PRIVATE nlohmann_json::nlohmann_json sodium)
else()
    target_link_libraries(railway_server PRIVATE nlohmann_json::nlohmann_json ${SODIUM_LIB})
endif()

# cpp-httplib 在 Linux 上需要 pthread
if(UNIX)
    target_link_libraries(railway_server PRIVATE pthread)
endif()

# Windows 交叉编译：链接 Winsock2（httplib 依赖）+ 静态链接运行时库
if(MINGW)
    target_link_libraries(railway_server PRIVATE ws2_32)
    target_link_options(railway_server PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ── 测试 ──
add_subdirectory(tests)
