# 12306 铁路票务系统

> C++17 铁路票务系统，模拟 12306 核心功能：查票、购票、退票、列车管理、审批流、RBAC 权限管控。

**20 站点 · 10 线路 · 100 列车 · 4 角色 RBAC · 纯 C++ 后端 + 零依赖前端**

---

## 目录结构

```
12306_realway_system/
├── server/
│   ├── src/
│   │   ├── main.cpp                    # 入口
│   │   ├── models.h                    # 全局数据模型（20个结构体+枚举）
│   │   ├── core/                       # HTTP 服务 + 路由 + 日志
│   │   │   ├── utils.h                 #   全局工具函数（UUID、时间、地理、序列查找、路线计算）
│   │   │   ├── server.h/.cpp           #   cpp-httplib 包装
│   │   │   ├── routes.h/.cpp           #   路由注册（10个端点）
│   │   │   └── logger.h/.cpp           #   日志基础设施
│   │   ├── data/                       # 数据加载 + 铁路网图 + 种子生成
│   │   │   ├── data_store.h/.cpp       #   单例数据加载器
│   │   │   ├── railway_graph.h/.cpp    #   邻接表 + JSON 持久化
│   │   │   └── train_generator.h/.cpp  #   100辆列车自动生成器
│   │   ├── auth/                       # 认证 + JWT + RBAC
│   │   │   ├── auth_service.h/.cpp     #   argon2id 密码哈希 + 用户管理
│   │   │   ├── jwt_service.h/.cpp      #   HS256 JWT 生成与校验
│   │   │   └── rbac_middleware.h/.cpp  #   std::bitset<64> 权限位图 + 中间件
│   │   ├── passenger/                  # 旅客端：查票 + 购票 + 退票
│   │   │   ├── train_query.h/.cpp      #   直达+换乘查询（车站-列车索引）
│   │   │   ├── order_service.h/.cpp    #   购票+退票（阶梯费率）
│   │   │   └── seat_inventory.h/.cpp   #   座位库存（细粒度 shared_mutex 锁）
│   │   ├── staff/                      # 铁路职工端（待实现）
│   │   └── admin/                      # 管理员端（待实现）
│   ├── config/
│   │   ├── stations.json               # 20个站点（呼包鄂地区）
│   │   ├── lines.json                  # 10条线路
│   │   ├── trains.json                 # 100辆列车（自动生成）
│   │   └── users.json                  # 用户数据
│   ├── frontend/
│   │   ├── index.html                  # SPA 入口
│   │   ├── style.css                   # 深色主题样式
│   │   └── app.js                      # 纯 JS 前端逻辑（零 npm 依赖）
│   └── CMakeLists.txt
├── scripts/
│   ├── build.sh                        # CMake 构建
│   └── run.sh                          # 启动服务
├── .claude/
│   ├── CLAUDE.md                       # 项目编码规范
│   └── skills/                         # AI 辅助 skill（code-review / doc-sync / …）
└── requirtment.md                      # 详细需求文档
```

---

## 技术栈

| 层 | 技术 | 说明 |
|----|------|------|
| 语言 | C++17 | `-Wall -Wextra -Wpedantic` |
| HTTP | cpp-httplib | header-only，零依赖 HTTP 服务 |
| JSON | nlohmann/json | 序列化/反序列化 |
| 加密 | libsodium | argon2id 密码哈希、HMAC-SHA256 JWT |
| 前端 | HTML/CSS/JS | 纯手写 SPA，零 npm 依赖 |
| 构建 | CMake | `cmake -S server -B build` |
| 部署 | Docker（计划） | Phase 10 |

---

## 快速开始

### 环境要求

- C++17 编译器（GCC 9+ / Clang 10+）
- CMake 3.14+
- libsodium

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libsodium-dev

# macOS
brew install cmake libsodium
```

### 构建与运行

```bash
cd 12306_realway_system

# 构建
bash scripts/build.sh

# 运行
bash scripts/run.sh
# 服务监听 http://0.0.0.0:8080
```

### 测试账号

| 角色 | 用户名 | 密码 |
|------|--------|------|
| 管理员 | `admin` | `admin123` |
| 审核员 | `approver` | `approver123` |
| 铁路职工 | `staff` | `staff123` |
| 普通旅客 | `passenger` | `pass123` |

---

## 功能概览

### 旅客端 ✅ 已实现

| 功能 | 说明 |
|------|------|
| 🔍 列车查询 | 直达 + 一次换乘（车站-列车索引 + 地理约束 + 换乘窗口 [10min, 3h]），14 天日期窗口 |
| 🎫 购票 | 原子下单，细粒度锁防超卖，逐段 Haversine 累加计价 |
| 🔄 退票 | 阶梯费率（>24h 退 95%、2-24h 退 90%、<2h 退 80%、发车后不可退） |
| 📋 订单查询 | 按状态筛选，时间倒序 |
| 🎨 前端 | 车型/时间/有票筛选 + 五种排序（发车/到达/用时/里程/票价） |

### 铁路职工端 ✅ 已实现

| 功能 | 说明 |
|------|------|
| 🚂 列车管理 | 新增/删除/时刻调整（均走审批） |
| ⚠️ 冲突检测 | 区间占用表 + 5 分钟安全裕量 |
| ✅ 审批流 | 四眼原则（Staff 提交 → Approver 审批）+ CAS 锁 + 二次冲突校验 |
| 🔐 角色拆分 | Staff（增删改列车）+ Approver（审批），权限互斥 |

### 管理员端 ⏳ 待实现（Phase 8-9）

| 功能 | 说明 |
|------|------|
| 👥 用户管理 | CRUD + 角色/禁用 |
| 📊 审计日志 | 链式 SHA256 防篡改 |
| ⚙️ 系统配置 | 票价倍率、退票费率 |
| 🗺️ 路网导出 | DOT 格式 + HTML 交互版 |

---

## API 参考

### 认证

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|:--:|
| POST | `/api/auth/login` | 登录，返回 JWT | — |

### 旅客

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|:--:|
| GET | `/api/trains/query?from=&to=&date=` | 查票（直达+换乘） | JWT |
| GET | `/api/trains/station?station=X&sort=departure\|train_id` | 车站查询 | JWT |
| GET | `/api/trains/{id}/stops` | 列车经停站详情 | JWT |
| POST | `/api/orders` | 购票 | JWT |
| GET | `/api/orders?status=` | 订单查询 | JWT |
| POST | `/api/orders/{id}/refund` | 退票 | JWT |

### 调试

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/health` | 健康检查 |
| GET | `/api/whoami` | 验证 JWT + 查看权限 |
| GET | `/api/debug/stations` | 站点列表 |
| GET | `/api/debug/graph?from=&to=` | 路网最短路径 |

---

## 技术亮点

### 线程安全 — 细粒度锁

```
每 (车次, 日期) 一个 std::shared_mutex
  查票 → shared_lock（读，允许并发）
  购票 → unique_lock（写，互斥）
  死锁预防 → 多锁按车次 ID 字典序
```

### 票价计算 — 沿轨道逐段累加

```
不是：Haversine(出发站, 到达站)  ← 直线距离，严重低估
而是：Σ Haversine(站ᵢ, 站ᵢ₊₁)   ← 沿 route_stations 逐段累加
```

使用 `route_stations`（含经过不停车的站）保证与实际铁路走向一致。

### RBAC 权限

```
std::bitset<64> 位图存储权限
每个 HTTP 请求 → 中间件提取 JWT → 校验权限位 → 放行/拒绝
四角色：Passenger / Staff / Approver / Admin
```

### 路网算法

```
RailwayGraph: 邻接表，启动时从 JSON 加载
TrainQuery:   直达（停站匹配）+ 换乘（车站-列车索引 + 地理约束）
```

### 数据安全

```
密码: argon2id (libsodium)，每个用户独立 salt
JWT:  HS256，30min 有效期
登录: 5 次失败锁定 30 分钟
```

---

## 种子数据

### 20 个站点（呼包鄂地区）

呼和浩特东、呼和浩特、包头、包头东、鄂尔多斯、东胜西、集宁南、乌兰察布、临河、乌海、准格尔、达拉特西、萨拉齐、察素齐、托克托东、土默特右旗、白云鄂博、乌审旗、卓资东、旗下营南

### 10 条线路

京包高铁、呼鄂城际、包西铁路、集包铁路、呼准鄂铁路、包兰铁路(内蒙段)、集呼高铁、呼临铁路、包白铁路、沿黄铁路

---

## 实现进度

```
✅ Phase 1  项目骨架         ✅ Phase 5  旅客前端
✅ Phase 2  数据模型          ✅ Phase 6  职工后端
✅ Phase 3  认证+RBAC         ✅ Phase 7  职工前端
✅ Phase 4  旅客后端          ⏳ Phase 8-10
```

详见 [requirtment.md](requirtment.md) 第八章实现优先级。

---

## 编码规范

- 一个 `.h` 配一个 `.cpp`，`snake_case` 命名
- `PascalCase` 类名，`camelCase` 函数，`snake_case_` 成员变量
- 内部工具函数放 `.cpp` 匿名 namespace，不放 `.h` private
- 禁止 `using namespace std`、裸 `new`/`delete`、硬编码路径
- 详见 [.claude/CLAUDE.md](.claude/CLAUDE.md)
