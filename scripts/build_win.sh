#!/usr/bin/env bash
# build_win.sh — 交叉编译 Windows x64 版本
# 用法: bash scripts/build_win.sh
# 前提: sudo apt install g++-mingw-w64-x86-64
# 功能: 交叉编译 libsodium → CMake MinGW 构建 → 打包 dist/railway-windows/
# 产物: dist/railway-windows/ 可直接压缩发给 Windows 用户双击运行
source "$(dirname "$0")/common.sh"

BUILD_DIR="$PROJECT_ROOT/build_win"   # 交叉编译用独立 build 目录
DIST_DIR="$PROJECT_ROOT/dist/railway-windows"
LIBSODIUM_VER="1.0.19"

# ── 步骤 1：检查/安装交叉编译器 ──
info "检查交叉编译器…"
if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
    echo "安装 mingw-w64 交叉编译器:"
    echo "  sudo apt install g++-mingw-w64-x86-64"
    exit 1
fi
success "交叉编译器就绪"

# ── 步骤 2：检查/交叉编译 libsodium ──
SODIUM_DIR="$PROJECT_ROOT/build_win_libsodium"
SODIUM_LIB="$SODIUM_DIR/lib/libsodium.a"

if [ ! -f "$SODIUM_LIB" ]; then
    info "交叉编译 libsodium…"
    mkdir -p "$SODIUM_DIR/src"
    LIBSODIUM_TAR="libsodium-${LIBSODIUM_VER}.tar.gz"

    if [ ! -f "$SODIUM_DIR/src/$LIBSODIUM_TAR" ]; then
        info "下载 libsodium-${LIBSODIUM_VER}…"
        wget "https://download.libsodium.org/libsodium/releases/$LIBSODIUM_TAR" \
            -O "$SODIUM_DIR/src/$LIBSODIUM_TAR" || {
            error "下载失败，请检查网络连接"
            exit 1
        }
    fi

    # 验证下载完整性
    if [ ! -s "$SODIUM_DIR/src/$LIBSODIUM_TAR" ]; then
        error "下载文件为空，删除后重试: rm $SODIUM_DIR/src/$LIBSODIUM_TAR"
        exit 1
    fi

    tar xzf "$SODIUM_DIR/src/$LIBSODIUM_TAR" -C "$SODIUM_DIR/src" || {
        error "解压失败，文件可能已损坏，请删除后重试"
        exit 1
    }

    # tar 内目录名可能是 libsodium-stable 而非 libsodium-X.Y.Z
    LIBSODIUM_SRC=""
    for d in "$SODIUM_DIR/src"/libsodium*/; do
        LIBSODIUM_SRC="${d%/}"
        break
    done
    if [ -z "$LIBSODIUM_SRC" ] || [ ! -d "$LIBSODIUM_SRC" ]; then
        error "解压后未找到 libsodium 源码目录"
        info "实际内容:"
        ls -la "$SODIUM_DIR/src/"
        exit 1
    fi
    info "源码目录: $LIBSODIUM_SRC"

    cd "$LIBSODIUM_SRC"

    ./configure \
        --host=x86_64-w64-mingw32 \
        --prefix="$SODIUM_DIR" \
        --disable-shared \
        --enable-static \
        --quiet
    make -j"$(nproc)" --quiet
    make install --quiet
    cd "$PROJECT_ROOT"
    success "libsodium 交叉编译完成"
else
    success "libsodium 已就绪"
fi

# ── 步骤 3：CMake 交叉编译 ──
info "交叉编译 railway_server.exe…"
cmake -S server -B "$BUILD_DIR" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_BUILD_TYPE=Release \
    -DSODIUM_LIB_PATH="$SODIUM_LIB" \
    -DSODIUM_INCLUDE_PATH="$SODIUM_DIR/include"

cmake --build "$BUILD_DIR" -j"$(nproc)"

success "产出: $BUILD_DIR/railway_server.exe"

# ── 步骤 4：打包分发目录 ──
info "打包分发目录…"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

cp "$BUILD_DIR/railway_server.exe" "$DIST_DIR/"
cp -r "$PROJECT_ROOT/server/config" "$DIST_DIR/"
cp -r "$PROJECT_ROOT/server/frontend" "$DIST_DIR/"
mkdir -p "$DIST_DIR/data"

# 复制 Windows 批处理启动脚本
cat > "$DIST_DIR/启动服务.bat" << 'BATEOF'
@echo off
chcp 65001 >nul
title 12306 铁路票务系统
echo.
echo   ╔══════════════════════════════════╗
echo   ║    12306 铁路票务系统            ║
echo   ║    服务地址: http://localhost:8080 ║
echo   ║    按 Ctrl+C 停止服务             ║
echo   ╚══════════════════════════════════╝
echo.
if not exist "data" mkdir "data"
railway_server.exe
pause
BATEOF

success "分发目录: $DIST_DIR"
echo ""
info "Windows 用户可以:"
echo "  1. 将 $DIST_DIR 整个文件夹压缩发给对方"
echo "  2. 对方解压后双击 '启动服务.bat' 即可使用"
echo "  3. 浏览器打开 http://localhost:8080"
