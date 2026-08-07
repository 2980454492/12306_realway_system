# 12306 铁路票务系统

一个 **C++17** 编写的铁路票务系统，模拟 12306 核心功能：列车余票查询、购票退票、列车管理、冲突检测、审批流、RBAC 五角色权限管控。系统划分为五个角色——**普通旅客**查票购票退票、**铁路职工**管理列车时刻与线路变更、**审核员**审批变更申请、**基础设施管理员**管理站点与线路、**系统管理员**管理用户与审计日志。每个角色职责单一、互不重叠，所有变更均通过审批流保证数据安全。

- **规模**：340 个站点 + 126 条线路 + 100 辆列车，覆盖全国地级市
- **并发**：`shared_mutex` 细粒度锁，100 线程并发抢票不超卖
- **安全**：argon2id 密码哈希、AES-256-GCM 加密、WAL 崩溃恢复、审计链式 SHA256
- **前端**：纯 HTML/CSS/JS SPA，零 npm 依赖，六套角色化界面
- **部署**：CMake 编译，支持 MinGW 交叉编译 Windows .exe

详细功能说明见 [MANUAL.md](MANUAL.md)。

---

## 快速开始

复制以下全部命令到终端执行：

```bash
# 1. 克隆项目
git clone https://github.com/2980454492/12306_realway_system.git
cd 12306_realway_system

# 2. 安装编译依赖
sudo apt update
sudo apt install -y build-essential cmake libsodium-dev nlohmann-json3-dev

# 3. 编译
bash scripts/build.sh

# 4. 启动（前台运行，Ctrl+C 停止）
bash scripts/run.sh
```

启动后浏览器打开 **http://localhost:8080**，用下方测试账号登录。

### 测试账号

| 角色 | 用户名 | 密码 |
|------|--------|------|
| 系统管理员 | `sys_admin` | `sys123` |
| 基础设施管理员 | `infra_admin` | `infra123` |
| 审核员 | `approver` | `approver123` |
| 铁路职工 | `staff` | `staff123` |
| 普通旅客 | `passenger` | `pass123` |

---

## 功能概览

### 旅客端

- 列车查询：直达 + 一次换乘，5 种排序 + 4 维筛选，换乘窗口 [10min, 3h]
- 购票：`shared_mutex` 细粒度锁防超卖，沿 stops 逐段 Haversine 累加计价
- 退票：阶梯费率（>24h 95%、2-24h 90%、<2h 80%、发车后不可退）
- 订单查询 + 车站查询（同城多站自动合并）

### 铁路职工端

- 列车管理：新增/修改/删除（均走审批），线路感知逐步选线 + 实时时速校验
- 冲突检测：区间占用表 key=`站A|站B|线路ID`，方向+线路隔离，5 分钟安全裕量
- 线路变更：处理 INFRA_ADMIN 生成的 DRAFT 审批，填写生效日期和停站时间后提交

### 审批端

- 六种审批类型：新增/修改/删除列车 + 线路加站/改站/删站
- 状态机：DRAFT → SUBMITTED → APPROVED / REJECTED
- 四眼原则 + `atomic_flag` CAS 锁 + 审批通过前二次冲突校验

### 管理员端

- INFRA_ADMIN：站点/线路 CRUD，修改线路自动检测站点变化并生成审批
- SYS_ADMIN：用户管理、审计日志（链式 SHA256）、系统配置（票价费率+退票费率）

---

## API 参考

| 方法 | 路径 | 说明 | 权限 |
|------|------|------|:---:|
| POST | `/api/auth/login` | 登录 | — |
| POST | `/api/auth/register` | 注册 | — |
| GET | `/api/stations` | 站点列表 | 登录 |
| GET | `/health` | 健康检查 | — |
| GET | `/api/trains/query?from=&to=&date=` | 查票（直达+换乘） | Passenger |
| GET | `/api/trains/station?station=X` | 车站查询 | Passenger |
| POST | `/api/orders` | 购票 | Passenger |
| POST | `/api/orders/{id}/refund` | 退票 | Passenger |
| GET | `/api/orders` | 订单查询 | Passenger |
| GET/POST/PUT/DELETE | `/api/admin/trains` | 列车管理 | Staff |
| GET | `/api/stations/neighbors` | 线路邻居索引 | Staff |
| GET | `/api/admin/stop-inserts` | 线路变更待办 | Staff |
| PUT | `/api/admin/approvals/{id}/stop-time` | 提交线路变更 | Staff |
| GET | `/api/admin/approvals` | 审批列表 | Staff / Approver |
| POST | `/api/admin/approvals/{id}/approve` | 审批通过 | Approver |
| POST | `/api/admin/approvals/{id}/reject` | 审批驳回 | Approver |
| GET/POST/PUT/DELETE | `/api/admin/stations` | 站点管理 | INFRA_ADMIN |
| GET/POST/PUT/DELETE | `/api/admin/lines` | 线路管理 | INFRA_ADMIN |
| GET/POST/PUT/DELETE | `/api/admin/users` | 用户管理 | SYS_ADMIN |
| GET | `/api/admin/audit` | 审计日志 | SYS_ADMIN |
| GET/PUT | `/api/admin/config` | 系统配置 | SYS_ADMIN |

---

## 技术栈

| 层 | 技术 |
|----|------|
| 语言 | C++17 (`-Wall -Wextra -Wpedantic`) |
| HTTP | cpp-httplib (header-only) |
| JSON | nlohmann/json |
| 加密 | libsodium (argon2id + HMAC-SHA256) |
| 前端 | 纯 HTML/CSS/JS SPA，零 npm 依赖 |
| 构建 | CMake (`cmake -S server -B build`) |

---

## 目录结构

```
12306_realway_system/
├── server/
│   ├── CMakeLists.txt                       # CMake 构建配置
│   ├── vendor/
│   │   └── httplib.h                        #   cpp-httplib（header-only HTTP 库）
│   ├── config/
│   │   ├── stations.json                    #   340 个站点种子数据
│   │   ├── lines.json                       #   126 条线路种子数据
│   │   ├── trains.json                      #   100 辆列车种子数据
│   │   ├── users.json                       #   种子用户（5 个角色）
│   │   ├── system.json                      #   系统配置（费率、限流参数）
│   │   └── key.bin                          #   AES-256 密钥文件
│   ├── src/
│   │   ├── main.cpp                         #   入口：初始化 + 信号处理 + 启动服务
│   │   ├── models.h                         #   全局数据模型（实体、枚举、序列化宏）
│   │   ├── config.h                         #   全局路径/端口/文件名常量
│   │   ├── utils.h                          #   工具函数（UUID、时间、Haversine、路线计算）
│   │   ├── data/
│   │   │   ├── data_store.h                 #     单例数据加载器（索引构建、CRUD）
│   │   │   └── data_store.cpp
│   │   ├── system/
│   │   │   ├── logger.h                     #     日志（控制台 + 文件，按日期切分）
│   │   │   ├── logger.cpp
│   │   │   ├── wal.h                        #     WAL 预写日志（append+fsync，崩溃恢复）
│   │   │   └── wal.cpp
│   │   ├── auth/
│   │   │   ├── auth_service.h               #     用户管理 + argon2id 密码哈希
│   │   │   ├── auth_service.cpp
│   │   │   ├── jwt_service.h                #     JWT 生成与校验
│   │   │   ├── jwt_service.cpp
│   │   │   ├── rbac_middleware.h            #     std::bitset<64> 权限位图 + 中间件
│   │   │   └── rbac_middleware.cpp
│   │   ├── security/
│   │   │   ├── crypto.h                     #     AES-256-GCM 加解密
│   │   │   ├── crypto.cpp
│   │   │   ├── rate_limiter.h               #     Token Bucket 限流
│   │   │   └── rate_limiter.cpp
│   │   ├── passenger/
│   │   │   ├── train_query.h                #     直达+换乘查询（车站-列车索引）
│   │   │   ├── train_query.cpp
│   │   │   ├── order_service.h              #     购票+退票（阶梯费率、原子库存扣减）
│   │   │   ├── order_service.cpp
│   │   │   ├── seat_inventory.h             #     座位库存（shared_mutex 细粒度锁）
│   │   │   └── seat_inventory.cpp
│   │   ├── staff/
│   │   │   ├── train_manager.h              #     列车增删改 + 运行图冲突检测
│   │   │   └── train_manager.cpp
│   │   ├── approver/
│   │   │   ├── approval_service.h           #     审批状态机 + 四眼原则 + CAS 锁
│   │   │   └── approval_service.cpp
│   │   ├── sys_admin/
│   │   │   ├── user_service.h               #     用户 CRUD
│   │   │   ├── user_service.cpp
│   │   │   ├── audit_service.h              #     审计日志（链式 SHA256）
│   │   │   ├── audit_service.cpp
│   │   │   ├── system_config.h              #     票价费率矩阵 + 退票费率
│   │   │   └── system_config.cpp
│   │   └── http/
│   │       ├── server.h                     #     cpp-httplib 包装（SSL、静态文件、多线程）
│   │       ├── server.cpp
│   │       ├── router.h                     #     路由注册入口
│   │       ├── router.cpp
│   │       ├── router_helpers.h             #     鉴权工具（checkAuth、badRequest、parseUint32）
│   │       ├── router_auth.cpp              #     登录/注册路由
│   │       ├── router_passenger.cpp         #     旅客路由（查票/订单/退票）
│   │       ├── router_staff.cpp             #     职工路由（列车/线路变更/提交）
│   │       ├── router_approver.cpp          #     审批路由（列表/通过/驳回）
│   │       ├── router_infra_admin.cpp       #     基础设施路由（站点/线路 CRUD）
│   │       └── router_sys_admin.cpp         #     系统管理路由（用户/审计/配置）
│   ├── data/                                # 运行时数据（WAL 日志、快照、审计日志，gitignore）
│   ├── frontend/
│   │   ├── index.html                       #   SPA 骨架 + 全部 <template>
│   │   ├── style.css                        #   深色主题样式（CSS 变量统一定义色值）
│   │   ├── app.js                           #   状态管理 + API 封装 + 工具函数 + UI 基类
│   │   ├── passenger.js                     #   旅客端（查票/购票/订单/车站查询）
│   │   ├── staff.js                         #   职工端（列车管理/线路变更/我的提交）
│   │   ├── approver.js                      #   审批端（审批列表/通过/驳回）
│   │   ├── infra.js                         #   基础设施管理端（站点/线路/路网图）
│   │   └── admin.js                         #   系统管理端（用户/审计/配置）
│   └── tests/
│       ├── CMakeLists.txt                   #   测试构建配置（Google Test）
│       ├── test_utils.cpp                   #   工具函数测试
│       ├── test_auth.cpp                    #   认证测试
│       ├── test_rbac.cpp                    #   权限测试
│       ├── test_data.cpp                    #   数据层测试
│       ├── test_passenger.cpp               #   旅客端测试
│       ├── test_staff.cpp                   #   职工端测试
│       ├── test_approver.cpp                #   审批端测试
│       └── test_admin.cpp                   #   管理员端测试
├── scripts/
│   ├── build.sh                             #   CMake 构建脚本（Linux）
│   ├── build_win.sh                         #   交叉编译 Windows .exe（MinGW）
│   ├── run.sh                               #   启动服务
│   ├── test.sh                              #   运行测试
│   ├── common.sh                            #   公共函数（info/warn/error 颜色输出）
│   ├── fetch_stations.py                    #   站点数据抓取脚本
│   └── station_data.py                      #   站点数据生成脚本
├── .claude/
│   ├── CLAUDE.md                            #   项目编码规范
│   ├── settings.json                        #   Claude Code 项目配置
│   └── skills/                              #   AI 辅助 skills（7 个）
├── MANUAL.md                                # 用户手册（功能说明 + API 表）
└── README.md                                # 本文件
```