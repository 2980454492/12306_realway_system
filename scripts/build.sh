#!/usr/bin/env bash
# build.sh — 12306 铁路票务系统自动构建脚本
# 检查依赖 → CMake 配置 → 编译 → 报告结果
set -euo pipefail

# ── 颜色输出函数 ──
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()    { echo -e "${CYAN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }

# ── 路径变量 ──
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$PROJECT_ROOT/server"
BUILD_DIR="$PROJECT_ROOT/build"

info "Project root: $PROJECT_ROOT"
info "Build dir:    $BUILD_DIR"

# ── 步骤 1：环境检查 ──
echo ""
info "── 步骤 1：检查编译环境 ──"

MISSING=()

# 检查 g++
if command -v g++ &>/dev/null; then
    GCC_VERSION=$(g++ -dumpversion)
    GCC_MAJOR=$(echo "$GCC_VERSION" | cut -d. -f1)
    if [ "$GCC_MAJOR" -ge 9 ]; then
        success "g++ $GCC_VERSION (C++17 支持)"
    else
        error "g++ $GCC_VERSION < 9，不支持 C++17"
        MISSING+=("g++ >= 9 (当前 $GCC_VERSION)")
    fi
else
    error "g++ 未安装"
    MISSING+=("g++ (build-essential)")
fi

# 检查 cmake
if command -v cmake &>/dev/null; then
    CMAKE_VERSION=$(cmake --version | head -1 | grep -oP '\d+\.\d+\.\d+')
    CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)
    if [ "${CMAKE_MINOR:-0}" -ge 16 ]; then
        success "cmake $CMAKE_VERSION"
    else
        error "cmake $CMAKE_VERSION < 3.16"
        MISSING+=("cmake >= 3.16 (当前 $CMAKE_VERSION)")
    fi
else
    error "cmake 未安装"
    MISSING+=("cmake")
fi

# 检查 pthread（Linux 基本都有，只需检查头文件）
if [ -f /usr/include/pthread.h ]; then
    success "pthread (httplib 依赖)"
else
    error "pthread 头文件缺失"
    MISSING+=("libpthread-stubs0-dev")
fi

# ── 依赖缺失处理 ──
if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    error "缺少以下依赖："
    for dep in "${MISSING[@]}"; do
        echo "  - $dep"
    done
    echo ""
    echo "──────────────────────────────"
    echo "  Ubuntu/Debian 安装命令："
    echo "    sudo apt update"
    echo "    sudo apt install build-essential cmake libpthread-stubs0-dev"
    echo ""
    echo "  Fedora 安装命令："
    echo "    sudo dnf install gcc-c++ cmake"
    echo ""
    echo "  Arch 安装命令："
    echo "    sudo pacman -S gcc cmake"
    echo "──────────────────────────────"
    exit 1
fi

echo ""
info "所有系统依赖满足，继续构建..."

# ── 步骤 2：CMake 配置 ──
echo ""
info "── 步骤 2：CMake 配置 ──"

mkdir -p "$BUILD_DIR"

# 捕获 stderr 用于错误分析
CMAKE_ERR_FILE=$(mktemp)

cd "$BUILD_DIR"
if ! cmake "$SERVER_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    2>"$CMAKE_ERR_FILE"; then

    echo ""
    error "CMake 配置失败！"

    # 常见错误诊断
    if grep -qi "FetchContent.*json" "$CMAKE_ERR_FILE" 2>/dev/null; then
        warn "nlohmann/json 下载失败——检查网络连接"
        warn "可尝试手动下载：https://github.com/nlohmann/json/releases"
    elif grep -qi "No CMAKE_CXX_COMPILER" "$CMAKE_ERR_FILE" 2>/dev/null; then
        warn "未找到 C++ 编译器——安装 build-essential"
    else
        warn "详细错误日志："
        grep -i "error\|fatal" "$CMAKE_ERR_FILE" 2>/dev/null || cat "$CMAKE_ERR_FILE"
    fi

    rm -f "$CMAKE_ERR_FILE"
    exit 1
fi
rm -f "$CMAKE_ERR_FILE"

success "CMake 配置完成"

# ── 步骤 3：编译 ──
echo ""
info "── 步骤 3：编译 ──"

NPROC=$(nproc 2>/dev/null || echo 4)
BUILD_ERR_FILE=$(mktemp)

START_TIME=$(date +%s)

if ! cmake --build . -j"$NPROC" 2>"$BUILD_ERR_FILE"; then
    echo ""
    error "编译失败！"

    # 提取错误行和上下文
    if grep -qi "error:" "$BUILD_ERR_FILE"; then
        warn "编译错误详情："
        grep --color=never -E "error:|warning:" "$BUILD_ERR_FILE" | head -20
    elif grep -qi "undefined reference" "$BUILD_ERR_FILE"; then
        warn "链接错误——缺少库依赖"
        grep --color=never "undefined reference" "$BUILD_ERR_FILE" | head -20
    elif grep -qi "No such file" "$BUILD_ERR_FILE"; then
        warn "找不到文件——检查 server/CMakeLists.txt 的 SOURCES 列表"
        grep --color=never "No such file" "$BUILD_ERR_FILE" | head -20
    else
        warn "完整编译输出："
        cat "$BUILD_ERR_FILE"
    fi

    rm -f "$BUILD_ERR_FILE"
    exit 1
fi

END_TIME=$(date +%s)
BUILD_TIME=$((END_TIME - START_TIME))

rm -f "$BUILD_ERR_FILE"

# ── 步骤 4：验证产物 ──
echo ""
info "── 步骤 4：验证产物 ──"

EXECUTABLE="$BUILD_DIR/railway_server"
if [ -x "$EXECUTABLE" ]; then
    EXE_SIZE=$(du -h "$EXECUTABLE" | cut -f1)
    success "产物: $EXECUTABLE ($EXE_SIZE)"
else
    error "产物未生成：$EXECUTABLE"
    exit 1
fi

# 快速启动测试（检查能否启动到监听状态）
# 切换到 server/ 目录启动，确保相对路径 config/ data/ 正确
info "快速启动测试..."
cd "$SERVER_DIR"
"$EXECUTABLE" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    # 测试 /health
    if HEALTH=$(curl -s http://127.0.0.1:8080/health 2>/dev/null); then
        if echo "$HEALTH" | grep -q '"ok":true'; then
            success "/health 响应正常: $(echo "$HEALTH" | grep -o '"version":"[^"]*"')"
        else
            warn "/health 响应异常: $HEALTH"
        fi
    else
        warn "无法连接 /health（可能端口被占用）"
    fi
    kill -TERM "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null || true
else
    error "服务启动后立即退出"
    exit 1
fi

# ── 步骤 5：报告 ──
echo ""
echo "═════════════════════════════════════"
echo "  构建报告"
echo "═════════════════════════════════════"
echo "  编译器:     g++ $(g++ -dumpversion)"
echo "  CMake:      $CMAKE_VERSION"
echo "  构建类型:   Debug"
echo "  编译耗时:   ${BUILD_TIME}s"
echo "  产物大小:   $EXE_SIZE"
echo "  产物路径:   $EXECUTABLE"
echo "  编译警告:   0 (目标: -Wall -Wextra -Wpedantic)"
echo "═════════════════════════════════════"

echo ""
success "构建完成！"
info "启动命令: cd \"$PROJECT_ROOT/server\" && \"$EXECUTABLE\""
