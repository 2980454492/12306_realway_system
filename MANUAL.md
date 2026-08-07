# 12306 铁路票务系统 — 用户手册

> **340 站点 · 126 线路 · 100 列车 · 5 角色 RBAC**
> C++17 + httplib + 文件存储(JSON+WAL) + 纯 HTML/CSS/JS 前端

---

## 一、项目概述

模拟中国铁路 12306 票务系统的核心功能。系统分为五个角色，每个角色有独立的页面和 API 权限，职责单一、互不重叠。

| 角色 | 职责 |
|------|------|
| 普通旅客 / PASSENGER | 查票、购票、退票、查看订单 |
| 铁路职工 / STAFF | 列车增删改、线路变更处理 |
| 审核员 / APPROVER | 审批通过/驳回 |
| 基础设施管理员 / INFRA_ADMIN | 站点/线路管理，自动生成线路变更审批 |
| 系统管理员 / SYS_ADMIN | 用户管理、审计日志、系统配置 |

| 维度 | 约束 |
|------|------|
| 站点 | 340 个（全国地级市，支持同城多站） |
| 线路 | 126 条（高铁、快铁、普铁） |
| 列车 | 100 辆（图定 ~90 + 临客 ~10） |
| 存储 | 文件存储（JSON）+ WAL 预写日志 |
| 前端 | 纯 HTML/CSS/JS，零框架依赖 |

---

## 二、角色功能

### 角色一：普通旅客（PASSENGER）

旅客可通过 Web 界面完成查票、购票、退票、订单查询。

#### 列车余票查询

`GET /api/trains/query?from=X&to=Y&date=Z`

- 输入：出发城市/站名、到达城市/站名、日期
- 支持直达和一次换乘
- 换乘窗口：≥ 10 分钟且 ≤ 3 小时，地理约束中转站在起止站之间
- 前端支持 5 种排序（发车/到达/历时/距离/票价）+ 4 维筛选（车型/出发站/到达站/只看有票）

**里程计算**：沿列车 stops 逐段累加 Haversine 距离，非直线距离。
**时刻表**：`stops` 含四种类型（0=始发、1=停靠、2=通过、3=终到），通过站不办客。

#### 购票与退票

| 操作 | API | 说明 |
|------|-----|------|
| 购票 | `POST /api/orders` | 选席位+数量+乘车人。`shared_mutex` 细粒度锁防超卖 |
| 退票 | `POST /api/orders/{id}/refund` | 阶梯费率：>24h 退95%、2-24h 退90%、<2h 退80%、发车后不可退 |
| 订单查询 | `GET /api/orders?status=X` | 按时间倒序，按状态筛选 |

#### 车站查询

`GET /api/trains/station?station=X&sort=departure|train_id`

输入城市名可查询该城市所有车站的经停列车。同城多站合并规则：优先始发站→终到站→最先停靠。

---

### 角色二：铁路职工（STAFF）

#### 新增列车

`POST /api/admin/trains`

线路感知逐步选线流程：
1. 输入列车种类 + 车次号 + 始发站 + 发车时间
2. `GET /api/stations/neighbors` 展示可选线路及邻居站
3. 逐站选择线路和停靠/通过状态，自动算速校验
4. 循环至终点站，设席位配置后提交审批

**校验规则**：车次号唯一、站点在注册集内、时间合法性、冲突检测（线路感知 key=站A|站B|线路ID，方向+线路隔离）、时速 ≤ min(列车时速, 线路时速)。

提交即写入 `trains.json`（PENDING），审批通过→ACTIVE+入占用表，驳回/撤回→ARCHIVED。

#### 修改 / 删除列车

| 操作 | API | 说明 |
|------|-----|------|
| 修改 | `PUT /api/admin/trains/{id}` | 同新增表单，预填现有数据，日期须≥15天 |
| 删除 | `DELETE /api/admin/trains/{id}` | 日期须≥15天，走审批流 |

#### 线路变更

`GET /api/admin/stop-inserts` — 查看 DRAFT 状态的线路变更审批。

INFRA_ADMIN 修改线路后自动生成。STAFF 填写生效日期（加站/改站还需填写到站/发车时间），提交后状态从 DRAFT→SUBMITTED，进入审批环节。

| 操作 | 审批类型 | STAFF 填写 | 审批通过后 |
|------|---------|-----------|----------|
| 加站 | STOP_INSERT (5) | 生效日期 + 到站/发车时间 | 插入新停站 |
| 改站 | STOP_REPLACE (7) | 生效日期 + 到站/发车时间 | 先删旧站再插新站 |
| 删站 | STOP_REMOVE (6) | 仅生效日期 | 移除该站 |

约束：生效日期 ≥ 15 天后；加站/改站须通过 `checkTrain()` 全量校验；改站校验时先用替换后的临时 stops 做检查。

**STAFF 全部 API**：

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/admin/trains` | 新增列车 |
| PUT | `/api/admin/trains/{id}` | 修改列车 |
| DELETE | `/api/admin/trains/{id}` | 删除列车 |
| GET | `/api/admin/trains` | 列车列表 |
| GET | `/api/stations/neighbors` | 车站-线路邻居索引 |
| GET | `/api/admin/stop-inserts` | 线路变更待办 |
| PUT | `/api/admin/approvals/{id}/stop-time` | 提交线路变更信息 |
| GET | `/api/admin/approvals` | 查看审批记录 |

---

### 角色三：审核员（APPROVER）

审批流覆盖六种操作类型：新增列车、调整时刻、删除列车、线路加站、线路改站、线路删站。

| 操作 | API | 说明 |
|------|-----|------|
| 审批通过 | `POST /api/admin/approvals/{id}/approve` | 四眼原则（非提交人）+ CAS 锁 + 二次冲突校验 |
| 审批驳回 | `POST /api/admin/approvals/{id}/reject` | 须填写驳回意见 |
| 审批列表 | `GET /api/admin/approvals?status=X` | 按状态/提交人/审批人筛选 |

**状态机**：`DRAFT` → `SUBMITTED`（STAFF 提交）→ `APPROVED`/`REJECTED`（APPROVER 决定），也可 `WITHDRAWN`（提交人撤回）。

线路加站/改站须 STAFF 先填充 `arrival+departure` 后才对 APPROVER 可见，删站须有 `effective_date`。

---

### 角色四：基础设施管理员（INFRA_ADMIN）

| 功能 | API | 说明 |
|------|-----|------|
| 站点列表 | `GET /api/admin/stations` | 查看全部站点 |
| 新增站点 | `POST /api/admin/stations` | 站名唯一，O(1) 查重 |
| 修改站点 | `PUT /api/admin/stations/{id}` | — |
| 删除站点 | `DELETE /api/admin/stations/{id}` | — |
| 线路列表 | `GET /api/admin/lines` | 查看全部线路 |
| 新增线路 | `POST /api/admin/lines` | — |
| 修改线路 | `PUT /api/admin/lines/{id}` | 自动检测站点变化，生成 DRAFT 审批 |
| 删除线路 | `DELETE /api/admin/lines/{id}` | — |

**自动审批生成规则**（修改线路时）：
- 新增站点 → 创建 STOP_INSERT
- 替换站点（同位置不同名）→ 创建 STOP_REPLACE
- 移除站点（无替换）→ 创建 STOP_REMOVE
- 有新增站时不生成 STOP_REMOVE（旧站视为被替换）

---

### 角色五：系统管理员（SYS_ADMIN）

| 功能 | API | 说明 |
|------|-----|------|
| 用户管理 | `GET/POST/PUT/DELETE /api/admin/users` | 创建/修改角色/禁用/删除 |
| 审计日志 | `GET /api/admin/audit?from=X&to=Y` | 链式 SHA256 校验，不可篡改，脱敏展示 |
| 系统配置 | `GET/PUT /api/admin/config` | 票价倍率、退票费率等，即时生效 |

---

## 三、审批流

审批流是 STAFF、APPROVER、INFRA_ADMIN 三角色的共享基础设施。

**状态机**：

| 状态 | 值 | 含义 | 下一状态 |
|------|:--:|------|---------|
| DRAFT | 4 | INFRA_ADMIN 已创建，等待 STAFF 填写 | SUBMITTED |
| SUBMITTED | 0 | 等待 APPROVER 审批 | APPROVED / REJECTED |
| APPROVED | 1 | 已通过，变更已应用 | — |
| REJECTED | 2 | 已驳回 | — |
| WITHDRAWN | 3 | 提交人撤回 | — |

**六种审批类型**：

| 类型 | 值 | 触发者 | 说明 |
|------|:--:|------|------|
| CREATE_TRAIN | 0 | STAFF | 新增列车 |
| ADJUST_SCHEDULE | 1 | STAFF | 调整时刻 |
| DELETE_TRAIN | 4 | STAFF | 删除列车 |
| STOP_INSERT | 5 | INFRA_ADMIN | 线路加站 |
| STOP_REMOVE | 6 | INFRA_ADMIN | 线路删站 |
| STOP_REPLACE | 7 | INFRA_ADMIN | 线路改站 |

**核心机制**：
- 四眼原则：`submitter_id ≠ approver_id`
- CAS 锁：`std::atomic_flag` 保证同一时刻只有一个审批在执行
- 二次冲突校验：审批通过前再次执行 `checkTrain()` 或 `detectConflicts()`

---

## 四、数据模型

### 核心实体

| 实体 | 文件 | 说明 |
|------|------|------|
| Station | `stations.json` | 340 个站点，含 id/name/city/lat/lon/type |
| Line | `lines.json` | 126 条线路，含 stations 序列/max_speed_kmh/type |
| Train | `trains.json` | 100 辆列车，含 stops 序列/seat_config/valid_from/status |
| ApprovalRequest | `approvals.json` | 审批记录，含 type/status/payload/submitter/approver |
| Order | `orders.json` | 购票订单，含 train_id/date/seat/price/status |

### 核心数据结构

```
区间占用表
  key = "出发站|到达站|线路ID"（方向敏感+线路隔离）
  value = set<(enter_time, leave_time)>

车站-线路-邻居索引
  map<station_id, vector<LineNeighbor>>
  启动时 buildIndexes() 中构建

车站-列车索引
  map<station_id, vector<TrainStopEntry>>
  启动时 buildIndexes() 中构建，O(1) 查票

座位库存
  key = (车次, 日期)
  shared_mutex 细粒度锁防超卖
```

---

## 五、API 参考

### 公共接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/login` | 登录 |
| POST | `/api/auth/register` | 注册 |
| POST | `/api/auth/logout` | 登出 |
| GET | `/api/stations` | 站点列表 |

### 旅客接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/trains/query?from=X&to=Y&date=Z` | 查票 |
| GET | `/api/trains/station?station=X` | 车站查询 |
| POST | `/api/orders` | 购票 |
| POST | `/api/orders/{id}/refund` | 退票 |
| GET | `/api/orders?status=X` | 订单查询 |

### 职工接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/admin/trains` | 列车列表 |
| POST | `/api/admin/trains` | 新增列车 |
| PUT | `/api/admin/trains/{id}` | 修改列车 |
| DELETE | `/api/admin/trains/{id}` | 删除列车 |
| GET | `/api/stations/neighbors` | 邻居索引 |
| GET | `/api/admin/stop-inserts` | 线路变更待办 |
| PUT | `/api/admin/approvals/{id}/stop-time` | 提交线路变更 |
| GET | `/api/admin/approvals` | 审批列表 |
| POST | `/api/admin/approvals/{id}/approve` | 审批通过 |
| POST | `/api/admin/approvals/{id}/reject` | 审批驳回 |

### 管理员接口

| 方法 | 路径 | 权限 |
|------|------|:---:|
| GET/POST/PUT/DELETE | `/api/admin/stations` | INFRA_ADMIN |
| GET/POST/PUT/DELETE | `/api/admin/lines` | INFRA_ADMIN |
| GET/POST/PUT/DELETE | `/api/admin/users` | SYS_ADMIN |
| GET | `/api/admin/audit` | SYS_ADMIN |
| GET/PUT | `/api/admin/config` | SYS_ADMIN |

---

## 六、种子数据

存储在 `server/config/` 目录下，启动时由 DataStore 加载。

| 文件 | 内容 |
|------|------|
| `stations.json` | 340 个站点（全国地级市，支持同城多站） |
| `lines.json` | 126 条线路（高铁/快铁/普铁，含站点序列+设计时速） |
| `trains.json` | 100 辆列车（图定+临客，含完整 stops + 席位配置） |
| `users.json` | 种子用户（infra_admin / sys_admin / approver / staff / passenger） |