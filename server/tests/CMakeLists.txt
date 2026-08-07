# tests/CMakeLists.txt — 测试构建配置
cmake_minimum_required(VERSION 3.16)

# 优先用系统包（apt install libgtest-dev），下载失败时回退到 FetchContent
find_package(GTest QUIET)
if(GTest_FOUND)
    set(GTEST_TARGET GTest::gtest_main)
else()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/release-1.12.1.zip
    )
    FetchContent_MakeAvailable(googletest)
    set(GTEST_TARGET gtest_main)
endif()

set(TEST_SOURCES
    test_utils.cpp
    test_auth.cpp
    test_rbac.cpp
    test_data.cpp
    test_passenger.cpp
    test_staff.cpp
    test_approver.cpp
    test_admin.cpp
)

add_executable(railway_tests ${TEST_SOURCES})

target_include_directories(railway_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor
)
if(SODIUM_INCLUDE_PATH)
    target_include_directories(railway_tests PRIVATE ${SODIUM_INCLUDE_PATH})
endif()

if(SODIUM_LIB_PATH)
    target_link_libraries(railway_tests PRIVATE
        ${GTEST_TARGET}
        nlohmann_json::nlohmann_json
        sodium
        ws2_32
    )
else()
    target_link_libraries(railway_tests PRIVATE
        ${GTEST_TARGET}
        nlohmann_json::nlohmann_json
        ${SODIUM_LIB}
        pthread
    )
endif()

# Add project sources (reuse the main project's source files)
set(PROJECT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/system/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/system/wal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sys_admin/audit_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sys_admin/system_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sys_admin/user_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/security/rate_limiter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/security/crypto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/data/data_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auth/auth_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auth/jwt_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auth/rbac_middleware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/passenger/train_query.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/passenger/seat_inventory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/passenger/order_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/staff/train_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/approver/approval_service.cpp
)
target_sources(railway_tests PRIVATE ${PROJECT_SOURCES})

add_test(NAME railway_tests COMMAND railway_tests)
