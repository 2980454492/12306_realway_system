# CMakeLists.txt — 12306 铁路票务系统构建配置
cmake_minimum_required(VERSION 3.16)
project(railway_server VERSION 0.1.0 LANGUAGES CXX)

# ── C++ 标准 ──
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
find_library(SODIUM_LIB sodium REQUIRED)

# ── 可执行目标 ──
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/server.cpp
    src/routes.cpp
    src/data_store.cpp
    src/railway_graph.cpp
    src/train_generator.cpp
    src/auth_service.cpp
    src/jwt_service.cpp
    src/rbac_middleware.cpp
    src/train_query.cpp
    src/seat_inventory.cpp
    src/order_service.cpp
)

set(HEADERS
    src/logger.h
    src/server.h
    src/routes.h
    src/models.h
    src/data_store.h
    src/train_generator.h
    src/railway_graph.h
    src/auth_service.h
    src/jwt_service.h
    src/rbac_middleware.h
    src/train_query.h
    src/seat_inventory.h
    src/order_service.h
    src/geo_utils.h
    vendor/httplib.h
)

add_executable(railway_server ${SOURCES} ${HEADERS})

# ── Include 路径 ──
target_include_directories(railway_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# ── 链接库 ──
# nlohmann/json (header-only, 但 CMake 包提供 target)
target_link_libraries(railway_server PRIVATE nlohmann_json::nlohmann_json ${SODIUM_LIB})

# cpp-httplib 在 Linux 上需要 pthread
if(UNIX)
    target_link_libraries(railway_server PRIVATE pthread)
endif()
