#!/usr/bin/env bash
# run.sh — 12306 铁路票务系统启动脚本
# 检查构建产物 → 启动服务 → 等待退出
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
EXECUTABLE="$BUILD_DIR/railway_server"

# ── 默认配置 ──
PORT="${PORT:-8080}"  # 可通过环境变量覆盖，与 Dockerfile / README 保持一致
HOST="${HOST:-127.0.0.1}"

info "项目根目录: $PROJECT_ROOT"
info "服务目录:   $SERVER_DIR"
info "可执行文件: $EXECUTABLE"

# ── 步骤 1：检查构建产物 ──
echo ""
info "── 步骤 1：检查构建产物 ──"

if [ ! -x "$EXECUTABLE" ]; then
    warn "未找到可执行文件: $EXECUTABLE"
    echo ""
    read -p "是否先执行构建？[Y/n] " -r REPLY
    REPLY="${REPLY:-Y}"
    if [[ "$REPLY" =~ ^[Yy]$ ]]; then
        BUILD_SCRIPT="$SCRIPT_DIR/build.sh"
        if [ -x "$BUILD_SCRIPT" ]; then
            info "正在构建..."
            "$BUILD_SCRIPT"
            if [ ! -x "$EXECUTABLE" ]; then
                error "构建完成但未找到产物，退出"
                exit 1
            fi
        else
            error "构建脚本不存在: $BUILD_SCRIPT"
            exit 1
        fi
    else
        error "未构建，退出"
        exit 1
    fi
else
    success "产物就绪: $EXECUTABLE"
fi

# ── 步骤 2：检查端口 ──
echo ""
info "── 步骤 2：检查端口 $PORT ──"

if command -v ss &>/dev/null; then
    if ss -tlnp | grep -q ":$PORT "; then
        warn "端口 $PORT 已被占用："
        ss -tlnp | grep ":$PORT " | head -3
        echo ""
        read -p "是否强制启动（可能失败）？[y/N] " -r REPLY
        if [[ ! "$REPLY" =~ ^[Yy]$ ]]; then
            error "已取消，退出"
            exit 1
        fi
    else
        success "端口 $PORT 空闲"
    fi
else
    info "未找到 ss 命令，跳过端口检查"
fi

# ── 步骤 3：检查数据目录 ──
echo ""
info "── 步骤 3：检查数据目录 ──"

mkdir -p "$SERVER_DIR/data"

# 保留 .gitkeep 不影响
if [ ! -f "$SERVER_DIR/data/.gitkeep" ]; then
    touch "$SERVER_DIR/data/.gitkeep"
fi

success "数据目录: $SERVER_DIR/data/"

# ── 步骤 4：启动服务 ──
echo ""
info "── 步骤 4：启动服务 ──"
echo ""

# 切换到 server/ 目录启动（日志和数据文件使用相对路径）
cd "$SERVER_DIR"

info "启动参数:"
info "  监听地址: http://0.0.0.0:$PORT"
info "  日志文件: $SERVER_DIR/data/server.log"
info "  工作目录: $(pwd)"
echo ""

# WSL2 用户提示
if grep -qi microsoft /proc/version 2>/dev/null; then
    WSL_IP=$(hostname -I | awk '{print $1}')
    info "检测到 WSL 环境，浏览器请访问:"
    info "  http://127.0.0.1:$PORT        (IPv4 显式地址，推荐)"
    info "  http://$WSL_IP:$PORT     (WSL 虚拟机 IP，备用)"
    info "  ⚠ 不要用 localhost——浏览器可能优先 IPv6 ::1 导致超时"
fi

# 注册信号处理：确保 Ctrl+C 能传递到子进程
cleanup() {
    info "收到退出信号，正在关闭服务..."
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    info "服务已关闭"
}
trap cleanup SIGINT SIGTERM EXIT

# 启动服务（前台运行，日志同时输出到文件和控制台）
"$EXECUTABLE" &
SERVER_PID=$!

info "服务 PID: $SERVER_PID"
info "健康检查: curl http://$HOST:$PORT/health"
echo ""
info "按 Ctrl+C 停止服务"
echo "═════════════════════════════════════"

# 等待服务退出
wait "$SERVER_PID"
