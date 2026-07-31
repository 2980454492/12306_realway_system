#!/usr/bin/env bash
# test.sh — 构建并运行全部测试
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== 步骤 1：CMake 配置 ==="
cmake -S "$PROJECT_DIR/server" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "=== 步骤 2：构建测试 ==="
cmake --build "$BUILD_DIR" --target railway_tests -j"$(nproc)"

echo ""
echo "=== 步骤 3：运行测试 ==="
mkdir -p "$PROJECT_DIR/server/data"
cd "$PROJECT_DIR/server" && "$BUILD_DIR/tests/railway_tests" "$@"
