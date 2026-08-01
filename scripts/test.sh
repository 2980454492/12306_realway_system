#!/usr/bin/env bash
# test.sh — 构建并运行全部测试
# 用法: bash scripts/test.sh [测试参数...]
# 功能: CMake 配置 → 构建 railway_tests → 运行测试
source "$(dirname "$0")/common.sh"

echo "=== 步骤 1：CMake 配置 ==="
cmake -S "$SERVER_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "=== 步骤 2：构建测试 ==="
cmake --build "$BUILD_DIR" --target railway_tests -j"$(nproc)"

echo ""
echo "=== 步骤 3：运行测试 ==="
mkdir -p "$SERVER_DIR/data"
cd "$SERVER_DIR" && "$BUILD_DIR/tests/railway_tests" "$@"
