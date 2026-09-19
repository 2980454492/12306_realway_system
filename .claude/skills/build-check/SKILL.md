---
name: build-check
description: Verify the 12306 railway project builds successfully before committing. Trigger when user is about to commit, wants to verify changes compile, or asks "does it build?".
---

## 构建检查流程

### 步骤 0：脚本入口

所有构建相关脚本均位于 `scripts/` 目录，使用方法：

| 脚本 | 用途 | 启动命令 |
|------|------|---------|
| `build.sh` | Linux 本地构建 + 冒烟测试 | `bash scripts/build.sh` |
| `run.sh` | 启动服务 | `bash scripts/run.sh` |
| `test.sh` | 构建并运行测试 | `bash scripts/test.sh` |
| `build_win.sh` | 交叉编译 Windows .exe | `bash scripts/build_win.sh` |

每个脚本文件头部均包含用途说明和启动命令。

---

### 步骤 1：环境依赖检查

检查以下工具是否安装，预测每种缺失的错误信息和修复建议：

#### 1.1 g++ (≥ 9)

```
✅ 期望输出:
  [OK] g++ 13.2.0 (C++17 支持)

❌ 缺失时输出:
  [ERROR] g++ 未安装

  修复:
    Ubuntu/Debian: sudo apt install build-essential
    Fedora:        sudo dnf install gcc-c++
    Arch:          sudo pacman -S gcc

❌ 版本过低:
  [ERROR] g++ 8.3.0 < 9，不支持 C++17

  修复:
    升级 GCC 到 9+，或使用 clang-10+
```

#### 1.2 cmake (≥ 3.16)

```
✅ 期望输出:
  [OK] cmake 3.28.3

❌ 缺失时输出:
  [ERROR] cmake 未安装

  修复:
    Ubuntu/Debian: sudo apt install cmake
    Fedora:        sudo dnf install cmake
    Arch:          sudo pacman -S cmake
    macOS:         brew install cmake

❌ 版本过低:
  [ERROR] cmake 3.10.2 < 3.16

  修复:
    从 https://cmake.org/download/ 下载新版本，或使用 pip install cmake
```

#### 1.3 libsodium

```
✅ 期望输出:
  [OK] libsodium (密码学库)

❌ CMake 配置阶段报错:
  -- Could NOT find sodium (missing: SODIUM_LIB)

  修复:
    Ubuntu/Debian: sudo apt install libsodium-dev
    Fedora:        sudo dnf install libsodium-devel
    Arch:          sudo pacman -S libsodium
    macOS:         brew install libsodium
```

#### 1.4 pthread

```
✅ 期望输出:
  [OK] pthread (httplib 依赖)

❌ 缺失时输出:
  [ERROR] pthread 头文件缺失

  修复:
    Ubuntu/Debian: sudo apt install libpthread-stubs0-dev
    (多数 Linux 发行版已内置，极少需要手动安装)
```

#### 1.5 Docker（可选）

```
❌ 缺失时输出:
  [ERROR] docker: 命令未找到

  修复:
    参考 https://docs.docker.com/engine/install/
```

#### 1.6 MinGW-w64（交叉编译 Windows 版本）

```
❌ 缺失时输出:
  [ERROR] x86_64-w64-mingw32-g++ 未安装

  修复:
    Ubuntu/Debian: sudo apt install g++-mingw-w64-x86-64
    Fedora:        sudo dnf install mingw64-gcc-c++
    Arch:          sudo pacman -S mingw-w64-gcc
```

---

### 步骤 2：CMake 配置 + 编译

```bash
rm -rf build/
mkdir -p build && cd build
cmake ../server -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

#### 2.1 CMake 配置阶段

**成功标志：** `Build files have been written to: .../build`

| 失败特征 | 原因 | 修复 |
|---------|------|------|
| `Could NOT find sodium` | libsodium-dev 未安装 | `sudo apt install libsodium-dev` |
| `No CMAKE_CXX_COMPILER could be found` | 编译器未安装 | `sudo apt install build-essential` |
| `CMake Error at CMakeLists.txt:3 (project)` | cmake 版本过低 | 升级 cmake ≥ 3.16 |
| `fatal error: httplib.h: No such file` | vendor/ 目录缺失 | 检查 `server/vendor/httplib.h` 是否存在 |
| `FetchContent ... json ... failed` | 网络不可达，无法下载 nlohmann/json | 检查网络；手动下载放到 `build/_deps/` |

#### 2.2 编译阶段

**成功标志：** `[100%] Built target railway_server`，无 warning

| 失败特征 | 原因 | 修复 |
|---------|------|------|
| `fatal error: models.h: No such file` | include 路径缺失 | 检查 `CMakeLists.txt` 的 `target_include_directories` |
| `undefined reference to 'sodium_init'` | libsodium 链接失败 | 检查 `target_link_libraries` 是否包含 `${SODIUM_LIB}` |
| `undefined reference to 'pthread_create'` | pthread 未链接 | Linux 需 `target_link_libraries(... pthread)` |
| `error: 'xxx' was not declared in this scope` | 缺少 include 或命名空间 | 检查头文件引用 |
| `error: new .cpp not in CMakeLists.txt` | 新增文件未加入 SOURCES 列表 | 将新文件路径加入 `server/CMakeLists.txt` |
| `warning: unused variable` | 代码中有未使用的变量 | 删掉或加 `(void)var;` |

#### 2.3 交叉编译（Windows）特殊问题

| 失败特征 | 原因 | 修复 |
|---------|------|------|
| `x86_64-w64-mingw32-g++: command not found` | MinGW 未安装 | `sudo apt install g++-mingw-w64-x86-64` |
| `cannot find -lsodium` | libsodium 未交叉编译 | `build_win.sh` 会自动编译，检查 `build_win_libsodium/` 目录 |
| `configure: error: ...` (libsodium ./configure) | 缺少 autotools | `sudo apt install autoconf automake libtool` |

---

### 步骤 3：快速冒烟测试

```bash
# 切换到 server/ 目录启动（配置文件使用相对路径）
cd server
../build/railway_server &
SERVER_PID=$!
sleep 2

# 健康检查
curl -s http://127.0.0.1:8080/health

kill $SERVER_PID
```

**成功标志：** `/health` 返回 `{"ok":true,"version":"..."}`

| 失败特征 | 原因 | 修复 |
|---------|------|------|
| `curl: (7) Failed to connect` | 服务未启动 | 检查端口是否被占用：`ss -tlnp \| grep 8080` |
| `curl: (56) Recv failure` | WSL localhost → IPv6 问题 | 用 `127.0.0.1` 替代 `localhost` |
| 服务启动后立即 crash | 配置文件缺失 | 确保 `server/config/` 下有 `stations.json` `lines.json` `trains.json` `users.json` |
| `Segmentation fault (core dumped)` | 空指针或越界 | 用 gdb 调试：`gdb --args ./build/railway_server` |
| `Failed to open: config/stations.json` | 工作目录不对 | 必须在 `server/` 目录下启动，或设 `cd server && ../build/railway_server` |

---

### 步骤 4：Docker 构建（可选）

```bash
docker build -t railway-server .
docker run --rm -p 8080:8080 railway-server
```

| 失败特征 | 原因 | 修复 |
|---------|------|------|
| `docker: command not found` | Docker 未安装 | 参考 https://docs.docker.com/engine/install/ |
| `COPY server/` 失败 | `.dockerignore` 排除了需要的文件 | 检查 `.dockerignore` |
| `cmake: command not found` (构建阶段) | Dockerfile 基础镜像未装 cmake | 确认用的是 `gcc:13-bookworm` 而非纯 `gcc:13` |

---

### 步骤 5：报告格式

```
═════════════════════════════════════
  构建报告
═════════════════════════════════════
  ✅ 环境检查: g++ 13.2, cmake 3.28, libsodium 已安装
  ✅ CMake 配置: 通过
  ✅ 编译: 通过 (12.3s, 0 warnings)
  ✅ 冒烟测试: /health 返回 200
  ✅ 产物: build/railway_server (8.5M)
═════════════════════════════════════
```

如有失败项，报告结尾应包含具体的 `sudo apt install ...` 修复命令。
