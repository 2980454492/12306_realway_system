# 12306 铁路票务系统 — 需求文档

> **项目定位**：生产级铁路票务系统，面向秋招面试展示技术深度
> **技术栈**：C++17 + Drogon/Crow + PostgreSQL/文件存储 + Docker
> **规模约束**：20个站点 / 10条主要线路 / 100辆列车 / 单用户视角简化

---

## 一、项目概述

### 1.1 背景

模拟中国铁路 12306 票务系统的核心功能。系统分为三大角色——**普通旅客**（查票、购票、退票、查看订单）、**铁路职工**（新增/删除列车、时刻表调整、线路管理）、**系统管理员**（用户管理、审批决策、系统配置）。系统须满足央企级技术标准：权限管控、合规审计、流程审批、数据安全、系统稳定性、并发控制。

### 1.2 范围与约束

| 维度 | 约束 |
|------|------|
| 站点数量 | 20个（以呼包鄂地区真实站名为种子数据） |
| 线路数量 | 10条主要线路（含高铁、普速、城际） |
| 列车数量 | 100辆（图定列车 ~90辆 + 临客 ~10辆） |
| 并发用户 | 模拟 100 并发购票请求 |
| 数据持久化 | 文件存储（JSON） + WAL 预写日志 |
| 对外接口 | RESTful HTTP API |
| 前端 | 不做（CLI或HTTP API即可，精力集中在后端） |

### 1.3 目标考核企业

| 目标企业 | 项目如何命中 |
|----------|-------------|
| 🔥 呼和浩特铁路局 | 直接对口：列车调度、运行图冲突检测、票务流程 |
| 银行科技岗 | 并发购票=金融交易；WAL=事务日志；审计链=合规 |
| 蒙西电网/国家能源 | 铁路网图算法=电网拓扑；Dijkstra=潮流计算 |
| 运营商 | 网络拓扑+资源分配=通信网络规划 |

---

## 二、功能需求

### F-1：旅客查票与购票

#### F-1.1 列车查询

**输入**：出发站、到达站、日期（yyyy-MM-dd）

**输出**：可用列车列表，每条包含：

| 字段 | 说明 |
|------|------|
| 车次 | 如 G2492 / K7901 / C1003 |
| 列车类型 | 高铁(G)/普速(K/Z)/城际(C)/临客(L) |
| 出发站 + 发车时间 | — |
| 到达站 + 到站时间 | — |
| 历时 | — |
| 余票数量 | 各席位（商务座/一等座/二等座/硬卧/硬座/无座）分别显示 |
| 票价 | 按席位×里程计算 |
| 经停站 | 完整停站序列 |

**查询逻辑**：
- 直达：遍历所有列车，找停站序列中同时包含起止站且顺序正确的
- 一次换乘：在铁路网图上 BFS 找中转站，两段列车的换乘时间窗口 ≥ 30 分钟
- 结果按历时排序

#### F-1.2 购票

**输入**：车次、日期、出发站、到达站、席位类型、数量(1-5张)、乘车人信息

**前置校验**：
1. 出发站和到达站在该车次停站序列中存在且顺序正确
2. 该日期该车次该席位余票 ≥ 请求数量
3. 同一天同一乘车人不能购买时间冲突的两张票

**并发保证**：购票操作须保证原子性——同一车次同一日期同一席位不允许超卖

**输出**：订单号(UUID)、车次、日期、乘车人、席位、票价、状态(已支付/已取消/已退票)

#### F-1.3 退票

**输入**：订单号

**逻辑**：
- 发车前 24 小时以上：退 95%
- 发车前 2-24 小时：退 90%
- 发车前 2 小时内：退 80%
- 发车后：不可退

**输出**：退款金额、订单状态变更为"已退票"、座位恢复可售

#### F-1.4 订单查询

**输入**：用户ID

**输出**：该用户所有订单，按时间倒序，支持按状态筛选

---

### F-2：铁路职工 — 列车管理

#### F-2.1 新增列车

**输入**：车次号、类型（图定/临客）、停站序列（每站：站名、到站时间、发车时间）、各席位座位数、有效期（临客须指定起止日期）

**前置校验**：

| 校验项 | 规则 |
|--------|------|
| 车次号唯一性 | 同车次号不允许重复激活 |
| 停站序列合法性 | 所有站点必须在系统注册的 20 站内 |
| 时间合法性 | 到站时间 < 发车时间（同一站）；第N站发车时间 < 第N+1站到站时间 |
| 🔥 运行图冲突检测 | 新增列车在每个运行区间 [站A→站B] 的时间占用段，必须与已有列车在该区间的占用无重叠（含 5 分钟安全裕量） |
| 临客有效期 | 临客须指定起止日期，过期自动停运 |

**冲突检测算法要求**：

对于新增列车的相邻停站构成的每个运行区间 `(站A, 站B, 进入时间, 离开时间)`：
1. 在区间占用表中查询 `站A→站B` 的所有已有占用记录
2. 对每条记录 `(已有进入时间, 已有离开时间)`，检查 `max(新增进入, 已有进入) < min(新增离开, 已有离开) + 5分钟裕量`
3. 任一区间冲突 → 全部拒绝，返回冲突详情（与哪个车次在哪个区间何时冲突）
4. 全部通过 → 写入区间占用表

**审批流**：
- 职工提交新增申请后，状态为 `PENDING_APPROVAL`
- 需另一职工（非提交人）审批通过后方可生效
- 审批通过前须**再次执行冲突检测**（乐观并发控制：审批时可能已有其他变更生效）
- 72小时未审批自动驳回

#### F-2.2 删除列车

**输入**：车次号

**前置校验**：
- 该车次存在且状态为"运行中"
- 该车次尚有未出发的已售车票 → 拒绝删除，提示"请先通知已购票旅客退票或改签"
- 无未出发已售车票 → 允许删除

**操作**：
- 清理该列车在区间占用表中的所有记录
- 列车状态标记为"已停运"
- 如果该车次在所有日期的票均已售罄或出发完毕，可彻底归档

#### F-2.3 调整时刻

**输入**：车次号、新的停站时间序列

**前置校验**：同新增列车的全部校验规则

**操作**：
- 先清理旧占用 → 执行冲突检测 → 通过后写入新占用
- 已购票旅客自动通知（记录通知事件，实际发短信/邮件不在本项目范围）
- 调整后的发车时间若早于已购票旅客预期，标记该订单为"需确认"

---

### F-3：铁路网管理

#### F-3.1 新增线路

**输入**：起点站、终点站、线路名称、里程(km)、设计时速、线路类型（高铁/普速/城际）

**校验**：
- 起点站和终点站不能相同
- 该线路不能与已有线路完全重复
- 端点站必须在系统注册站内（若需要新增站点，调用 F-3.2 先建站）

**操作**：
- 在铁路网图中添加边（双向）
- 标记已有最短路径缓存失效
- 审批流：需管理员审批

#### F-3.2 新增站点

**输入**：站名、所属城市、站类型（高铁站/普速站/枢纽站）、经纬度(可选)

**校验**：
- 站名在 20 站范围内不重复
- 站名仅允许中文字符+限定集合（白名单校验）

**操作**：
- 站点集合中新增
- 后续可以将其连入铁路网（通过 F-3.1）

---

### F-4：铁路网可视化导出

#### F-4.1 导出 Graphviz DOT 格式

**输出**：`railway_net.dot` 文件

**格式要求**：
- 站点作为节点，标注站名和类型（不同形状：高铁=椭圆/普速=矩形/枢纽=六边形）
- 线路作为边，标注线路名称和里程
- 不同线路类型用不同颜色（高铁=红色/普速=蓝色/城际=绿色）

**使用方式**：`dot -Tpng railway_net.dot -o railway_net.png` 生成 PNG 图片

#### F-4.2 导出 HTML 交互版（可选加分项）

**输出**：`railway_net.html` 文件

**功能**：
- 使用 vis.js 或 ECharts 力导向图
- 鼠标悬停站点显示详情
- 点击线路高亮该线路上的所有车次
- 浏览器直接打开，无需任何依赖

---

## 三、非功能需求

### NF-1：权限管控（RBAC）

```
普通旅客 (Passenger)
  ├── 查票
  ├── 购票
  ├── 退票（仅限自己的订单）
  └── 查看自己订单

铁路职工 (Staff)
  ├── 继承旅客全部权限
  ├── 新增列车（需审批）
  ├── 删除列车
  ├── 调整时刻（需审批）
  ├── 新增站点/线路（需审批）
  ├── 查看本部门审计日志
  └── 审批他人的申请

系统管理员 (Admin)
  ├── 继承职工全部权限
  ├── 管理用户（创建/禁用/删除）
  ├── 审批决策（最终驳回/通过权）
  ├── 查看全部审计日志
  ├── 系统配置（票价倍率、退票费率等）
  └── 否决已有审批结果
```

**技术要求**：

| 要求 | 实现说明 |
|------|----------|
| 权限存储 | `std::bitset<64>` 位图，每个bit代表一个权限。角色-权限映射表 `unordered_map<Role, bitset>` |
| Token 认证 | JWT（RS256签名），载荷含 userId、role、exp。每个HTTP请求从 Authorization header 提取并校验 |
| 中间件拦截 | 请求进入后在中间件层完成认证+鉴权，未通过直接返回 401/403，不进入业务逻辑 |
| 密码存储 | argon2id 哈希（libsodium），每个用户独立 salt，参数 t=3, m=65536, p=4 |
| 账号锁定 | 同一账号5次密码错误锁定30分钟 |

### NF-2：合规审计

| 要求 | 实现说明 |
|------|----------|
| 审计记录内容 | 操作时间、操作人(用户ID+角色)、操作类型、操作目标、操作详情(JSON)、操作结果、IP地址、会话ID |
| 链式校验(不可篡改) | 每条审计记录 = `SHA256(自己的hash + 上一条的hash)`。任一记录被修改，后续全部校验失败 |
| 双写策略 | 审计日志同时写本地文件 + 内存环形缓冲(2048条滚动)。磁盘满时降级为仅内存缓冲 |
| 敏感数据脱敏 | 输出前脱敏层：身份证→`37****199001010011`、手机号→`138****1234` |
| 查询过滤 | 按时间范围、操作类型、操作人、操作结果过滤。Staff 仅可查自己操作，Admin 可查全部 |
| 定期归档 | 每月生成审计报告摘要(MD)，审计日志归档压缩 |

### NF-3：流程审批

| 要求 | 实现说明 |
|------|----------|
| 审批对象 | 新增列车、调整时刻、新增线路、新增站点 |
| 状态机 | DRAFT → SUBMITTED → APPROVED / REJECTED / EXPIRED |
| 审批人限制 | 提交人 ≠ 审批人（四眼原则，built-in check） |
| 🔥 审批时二次冲突校验 | 审批通过前再次执行冲突检测。提交时的快照 vs 当前实际状态，不一致则驳回并显示差异 |
| 超时处理 | 72小时未审批自动标记 EXPIRED。Timer Wheel 数据结构遍历待审批列表 |
| 可追溯 | 完整的审批链：谁提交→谁审批→时间→意见→最终状态 |

### NF-4：数据安全

| 要求 | 实现说明 |
|------|----------|
| 传输加密 | HTTPS (Crow/Drogon 配置 OpenSSL TLS 1.3) |
| 静态数据加密 | 用户敏感字段（密码哈希、手机号、身份证）用 AES-256-GCM 加密后存盘（libsodium） |
| 输入校验 | 白名单验证。站名仅允许系统注册站名集合。日期格式严格校验。防路径穿越攻击 |
| 会话安全 | Access Token 30min + Refresh Token 7天。登出后 Token 加入内存黑名单 |
| 防重放 | 请求附带 nonce + timestamp，服务端缓存已处理的 nonce（TTL 5分钟） |

### NF-5：系统稳定性

| 要求 | 实现说明 |
|------|----------|
| 🔥 WAL 预写日志 | 任何写操作先 append 到 WAL 文件（`fsync` 确保落盘），再修改内存数据。崩溃恢复时重放 WAL |
| Checkpoint | 每 5 分钟或 WAL 达 10MB 时，全量快照到快照文件 + 截断已持久化的 WAL |
| 优雅关闭 | 注册 SIGINT/SIGTERM 信号处理：停止接受新请求 → 等待进行中请求完成 → 刷 WAL → 退出 |
| 线程池 | `std::thread` + 无锁队列实现，固定大小为 `std::thread::hardware_concurrency()` |
| 健康检查 | `/health` 端点返回：内存使用率、请求 QPS、pending 审批数、磁盘剩余、WAL 大小 |
| 资源限制 | 单用户 QPS 限制(Token Bucket)，防止恶意刷票。每个 IP 每分钟最多 60 次查票、10 次购票尝试 |

### NF-6：并发控制

| 场景 | 要求 | 实现说明 |
|------|------|----------|
| 🔥 购票并发 | 同车次同日期同席位不超卖 | 每 (车次, 日期, 席位) 一个 `std::shared_mutex`。查票用 `shared_lock`（读），购票用 `unique_lock`（写）。乐观锁兜底：座位表带 `version` 字段，CAS 校验 |
| 审批并发 | 同一申请被多人同时审批 | `std::atomic_flag` CAS 操作，第一个成功的获得审批权，其余返回"已被处理" |
| 时刻表并发 | 列车运行图更新时不影响查票 | `std::shared_mutex` 保护运行图。多读少写，写时独占 |
| 审计日志并发 | 多线程同时写审计日志不丢数据 | 无锁队列(Lock-Free Queue)：业务线程 `push`，专用 logger 线程 `pop` 并写入磁盘 |
| 死锁预防 | 多锁场景不卡死 | 两把及以上锁时，按车次 ID 字典序固定加锁顺序 |

---

## 四、数据模型

### 4.1 核心实体

```
Station
  id: uint32_t            # 站ID，唯一
  name: string             # 站名（如"呼和浩特东"）
  city: string             # 所属城市
  type: StationType        # HIGH_SPEED / NORMAL / HUB
  latitude: double         # 经度(可选，可视化用)
  longitude: double        # 纬度(可选，可视化用)

Line
  id: uint32_t
  name: string             # 线路名（如"京包高铁"）
  station_a: uint32_t      # 端点站ID
  station_b: uint32_t
  distance_km: double      # 里程
  max_speed_kmh: uint32_t  # 设计时速
  type: LineType           # HIGH_SPEED / NORMAL / INTERCITY

Train
  id: string               # 车次号（如"G2492"）
  type: TrainType           # REGULAR(图定) / TEMPORARY(临客)
  stops: vector<Stop>      # 停站序列
  status: TrainStatus      # ACTIVE / SUSPENDED / ARCHIVED
  seat_config: SeatConfig  # 各席位座位数

Stop
  station_id: uint32_t
  arrival: TimePoint       # 到站时间(HH:MM)
  departure: TimePoint     # 发车时间(HH:MM)
  platform: uint8_t        # 站台号(可选)

SeatConfig
  business_seats: uint16_t
  first_seats: uint16_t
  second_seats: uint16_t
  hard_sleeper: uint16_t
  hard_seat: uint16_t
  no_seat: uint16_t

Order
  id: string               # UUID
  user_id: string
  train_id: string
  date: Date
  from_station: uint32_t
  to_station: uint32_t
  seat_type: SeatType
  seat_number: uint16_t
  price: double            # 票价(分)
  status: OrderStatus      # PAID / CANCELLED / REFUNDED
  created_at: TimePoint
  passenger_name: string   # 乘车人
  passenger_id: string     # 身份证号(脱敏存储)

ApprovalRequest
  id: string               # UUID
  type: ApprovalType        # CREATE_TRAIN / ADJUST_SCHEDULE / ADD_LINE / ADD_STATION
  submitter_id: string
  approver_id: string       # 审批人，提交时为空
  status: ApprovalState     # DRAFT / SUBMITTED / APPROVED / REJECTED / EXPIRED
  payload: json             # 变更内容
  snapshot: json            # 提交时的状态快照（用于审批时二次冲突校验）
  submitted_at: TimePoint
  decided_at: TimePoint     # 审批决定时间
  comment: string           # 审批意见(驳回时必填)

AuditRecord
  id: string               # UUID
  timestamp: TimePoint
  operator_id: string
  operator_role: string
  action: string            # 操作类型枚举
  target: string            # 操作目标
  detail: json              # 操作详情
  result: string            # SUCCESS / FAILURE / PENDING
  ip: string
  session_id: string
  prev_hash: string         # 上一条审计记录的SHA256（链式校验）
  checksum: string          # 本条的SHA256
```

### 4.2 核心数据结构

```
铁路网图 RailwayGraph
  adjacency: map<StationId, vector<pair<StationId, Distance>>>
  shortest_cache: map<pair<StartId, EndId>, PathResult>  # 最短路径缓存
  # 缓存失效标记：每次新增线路后标记脏

区间占用表 IntervalOccupancy
  # key = (站A, 站B) 即运行区间
  # value = set<TrainInterval> 按进入时间排序
  occupancy: map<pair<StationId, StationId>, set<TrainInterval, TimeCompare>>

TrainInterval
  train_id: string
  date: Date
  enter_time: TimePoint      # 进入该区间的时刻
  leave_time: TimePoint      # 离开该区间的时刻

座位表 SeatInventory
  # key = (车次, 日期)
  # value = 各席位座位占用位图
  seats: map<pair<TrainId, Date>, SeatBitmap>
  mutexes: map<pair<TrainId, Date>, shared_mutex>  # 细粒度锁

SeatBitmap
  business: vector<bool>     # 商务座每个座位是否已售
  first: vector<bool>
  second: vector<bool>
  hard_sleeper: vector<bool>
  hard_seat: vector<bool>
  version: uint64_t          # 乐观锁版本号
```

---

## 五、API 设计概要

### 5.1 旅客接口

| 方法 | 路径 | 说明 | 权限 |
|------|------|------|:---:|
| POST | `/api/auth/login` | 登录获取Token | - |
| POST | `/api/auth/refresh` | 刷新Token | - |
| POST | `/api/auth/logout` | 登出 | Passenger+ |
| GET | `/api/trains/query?from=X&to=Y&date=Z` | 查票 | Passenger+ |
| GET | `/api/trains/{id}/stops` | 查询列车经停站 | Passenger+ |
| POST | `/api/orders` | 购票 | Passenger+ |
| POST | `/api/orders/{id}/refund` | 退票 | Passenger+(仅自己订单) |
| GET | `/api/orders?status=X` | 订单查询 | Passenger+ |

### 5.2 铁路职工接口

| 方法 | 路径 | 说明 | 权限 |
|------|------|------|:---:|
| POST | `/api/admin/trains` | 新增列车（提交审批） | Staff+ |
| DELETE | `/api/admin/trains/{id}` | 删除列车 | Staff+ |
| PUT | `/api/admin/trains/{id}/schedule` | 调整时刻（提交审批） | Staff+ |
| POST | `/api/admin/stations` | 新增站点（提交审批） | Staff+ |
| POST | `/api/admin/lines` | 新增线路（提交审批） | Staff+ |
| POST | `/api/admin/approvals/{id}/approve` | 审批通过 | Staff+ (非提交人) |
| POST | `/api/admin/approvals/{id}/reject` | 审批驳回 | Staff+ (非提交人) |
| GET | `/api/admin/approvals?status=X` | 查看审批列表 | Staff+ |

### 5.3 管理员接口

| 方法 | 路径 | 说明 | 权限 |
|------|------|------|:---:|
| POST | `/api/admin/users` | 创建用户 | Admin |
| PUT | `/api/admin/users/{id}` | 修改用户（角色/状态） | Admin |
| GET | `/api/admin/audit?from=X&to=Y` | 查询审计日志 | Admin |
| GET | `/api/admin/config` | 查看系统配置 | Admin |
| PUT | `/api/admin/config` | 修改系统配置 | Admin |
| GET | `/api/admin/export/network/dot` | 导出铁路网(DOT) | Admin |
| GET | `/api/admin/export/network/html` | 导出铁路网(HTML) | Admin |

---

## 六、种子数据

### 6.1 20个站点（呼包鄂地区）

| 序号 | 站名 | 城市 | 类型 |
|:---:|------|------|:---:|
| 1 | 呼和浩特东 | 呼和浩特 | 枢纽站 |
| 2 | 呼和浩特 | 呼和浩特 | 普速站 |
| 3 | 包头 | 包头 | 枢纽站 |
| 4 | 包头东 | 包头 | 普速站 |
| 5 | 鄂尔多斯 | 鄂尔多斯 | 枢纽站 |
| 6 | 东胜西 | 鄂尔多斯 | 普速站 |
| 7 | 集宁南 | 乌兰察布 | 普速站 |
| 8 | 乌兰察布 | 乌兰察布 | 高铁站 |
| 9 | 临河 | 巴彦淖尔 | 普速站 |
| 10 | 乌海 | 乌海 | 普速站 |
| 11 | 准格尔 | 鄂尔多斯 | 普速站 |
| 12 | 达拉特西 | 鄂尔多斯 | 普速站 |
| 13 | 萨拉齐 | 包头 | 普速站 |
| 14 | 察素齐 | 呼和浩特 | 普速站 |
| 15 | 托克托东 | 呼和浩特 | 高铁站 |
| 16 | 土默特右旗 | 包头 | 普速站 |
| 17 | 白云鄂博 | 包头 | 普速站 |
| 18 | 乌审旗 | 鄂尔多斯 | 普速站 |
| 19 | 卓资东 | 乌兰察布 | 高铁站 |
| 20 | 旗下营南 | 乌兰察布 | 高铁站 |

### 6.2 10条主要线路

| 序号 | 线路名 | 起止 | 类型 | 里程(km) |
|:---:|------|------|:---:|:---:|
| 1 | 京包高铁 | 呼和浩特东 ↔ 包头 | 高铁 | 165 |
| 2 | 呼鄂城际 | 呼和浩特东 ↔ 鄂尔多斯 | 城际 | 210 |
| 3 | 包西铁路 | 包头 ↔ 鄂尔多斯 | 普速 | 130 |
| 4 | 集包铁路 | 集宁南 ↔ 包头 | 普速 | 320 |
| 5 | 呼准鄂铁路 | 呼和浩特 ↔ 准格尔 | 普速 | 160 |
| 6 | 包兰铁路(内蒙段) | 包头 ↔ 乌海 | 普速 | 405 |
| 7 | 集呼高铁 | 乌兰察布 ↔ 呼和浩特东 | 高铁 | 135 |
| 8 | 呼临铁路 | 呼和浩特 ↔ 临河 | 普速 | 385 |
| 9 | 包白铁路 | 包头 ↔ 白云鄂博 | 普速 | 150 |
| 10 | 沿黄铁路 | 鄂尔多斯 ↔ 乌海 | 普速 | 380 |

---

## 七、验收标准

### 7.1 功能验收

- [x] 旅客可查询直达和一次换乘列车，结果准确
- [x] 旅客可购票，同一车次日期席位不超卖
- [x] 旅客可退票，按退票时间正确计算退款
- [x] 职工可新增列车，冲突检测拒绝时刻冲突的列车
- [x] 职工可删除无未出发已售车票的列车
- [x] 新增列车须审批通过后方可生效
- [x] 审批时二次冲突校验正确工作（乐观并发控制）
- [x] 管理员可新增线路和站点，路网图正确更新
- [x] 可导出铁路网 DOT 文件和 HTML 文件

### 7.2 非功能验收

- [x] RBAC 权限：Passenger 无法调用 Staff/Admin 接口
- [x] 审计日志：所有敏感操作可追溯，链式校验有效
- [x] 数据安全：密码 argon2id 哈希、敏感字段 AES 加密
- [x] 系统稳定性：WAL 崩溃恢复测试通过（kill -9 后重启不丢数据）
- [x] 并发测试：100线程同时抢1张票，最终只有1个成功
- [x] 线程安全：Thread Sanitizer 报告无 data race

### 7.3 代码质量

- [x] 含注释约14000-16000行
- [x] 单元测试覆盖率（核心模块 > 80%）
- [x] 编译无警告（`-Wall -Wextra -Wpedantic`）
- [x] Thread Sanitizer 通过
- [x] Address Sanitizer 通过

---

## 八、实现优先级

> 按依赖关系分 12 个阶段，每个阶段产出可演示的增量。预计总代码量 14000-16000 行。

### 第一阶段：项目骨架（预计 2 天）

**目标**：能编译、能启动、能响应 `/health`

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | CMake 工程搭建，C++17 + Drogon/Crow 依赖引入 | `server/CMakeLists.txt` 可配置编译 |
| P0 | HTTP Server 启动，`/health` 端点返回 200 | 服务可启动，curl 可达 |
| P0 | 基础目录结构 + `.gitignore` | `server/src/` `server/config/` `server/data/` |
| P0 | 日志基础设施（控制台 + 文件） | 统一日志接口 |

**可演示**：`curl localhost:8080/health` → `{"ok":true,"uptime":...}`

---

### 第二阶段：数据模型 + 种子数据（预计 3 天）

**依赖**：第一阶段

**目标**：所有核心实体定义完成，种子数据可加载

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 定义核心实体：`Station` `Line` `Train` `Stop` `SeatConfig` `Order` | `models.h` 所有 struct + `to_json`/`from_json` |
| P0 | 20 个站点 + 10 条线路种子数据 | `server/config/stations.json` `lines.json` |
| P0 | 铁路网图 `RailwayGraph`：邻接表 + Dijkstra 最短路径 | 换乘查询的底层数据结构 |
| P1 | 100 辆列车种子数据生成器（含停站序列、席位配置） | `server/config/trains.json` |
| P1 | 数据加载器：启动时从 JSON 加载到内存 | `DataStore` 基础读写 |

**可演示**：启动后内存中已有完整路网，可手动调用内部接口验证图算法。

---

### 第三阶段：认证与 RBAC（预计 3 天）

**依赖**：第二阶段

**目标**：用户体系 + JWT + 权限中间件就绪

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 用户模型 + argon2id 密码哈希（libsodium） | 用户注册/存储/验证 |
| P0 | JWT 生成与校验（RS256），包含 userId + role + exp | `AuthService` |
| P0 | RBAC 权限位图：`std::bitset<64>`，三角色权限映射表 | `RbacMiddleware` |
| P0 | 认证中间件：从 Authorization header 提取 Token → 注入请求上下文 | 所有受保护路由自动校验 |
| P1 | `/api/auth/login` `/api/auth/refresh` `/api/auth/logout` | 三个认证端点 |
| P1 | Token 黑名单（登出后失效）、账号锁定（5次错误/30min） | 安全加固 |
| P1 | 预置种子用户（admin / staff / passenger 各一个） | 方便开发测试 |

**可演示**：登录获取 Token，用不同角色 Token 访问接口得到不同权限结果。

---

### 第四阶段：旅客查票（预计 3 天）

**依赖**：第三阶段

**目标**：旅客可查询直达 + 一次换乘列车

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 直达查询：遍历所有列车，匹配停站序列 | `GET /api/trains/query?from=X&to=Y&date=Z` |
| P0 | 一次换乘查询：铁路网图上 BFS 找中转站，换乘 ≥ 30min | 同上接口，`transfer` 字段 |
| P0 | 结果排序（按历时）、票价计算（按里程×席位） | 完整查询结果 |
| P1 | 列车经停站详情 | `GET /api/trains/{id}/stops` |
| P1 | 最短路径缓存（新增线路时标记脏） | 查询性能优化 |

**可演示**：curl 查票，看到直达 + 换乘结果，历时排序正确。

---

### 第五阶段：购票 + 并发控制（预计 3 天）

**依赖**：第四阶段

**目标**：能下单、不超卖、100 并发抢 1 张票只有 1 人成功

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 座位库存 `SeatInventory`：`(车次, 日期) → SeatBitmap` | 库存数据结构 |
| P0 | 购票接口：校验 → 扣库存 → 生成订单 → 返回 UUID | `POST /api/orders` |
| P0 | **细粒度锁**：每 `(车次, 日期, 席位)` 一个 `shared_mutex` | 不超卖的核心保证 |
| P0 | 乐观锁兜底：`version` 字段 CAS 校验 | 双重保险 |
| P0 | 死锁预防：多锁场景按车次 ID 字典序加锁 | 不卡死 |
| P1 | 乘车人冲突检测（同一天不卖时间重叠的两张票） | 业务校验 |
| P1 | 订单查询：按用户ID、按状态筛选、按时间倒序 | `GET /api/orders?status=X` |

**可演示**：100 线程并发抢最后 1 张票，最终 exactly 1 人成功，座位数正确。

---

### 第六阶段：退票（预计 1 天）

**依赖**：第五阶段

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 退票逻辑：校验订单状态 → 计算退款（按时间阶梯费率）→ 恢复座位 | `POST /api/orders/{id}/refund` |
| P1 | 权限校验：只能退自己的订单 | RBAC + 订单 owner 校验 |

**可演示**：购票后退票，座位数恢复，退款金额按费率正确计算。

---

### 第七阶段：铁路职工 — 列车管理 + 冲突检测（预计 4 天）

**依赖**：第三阶段（RBAC）

**目标**：职工可增删列车，冲突检测拒绝时刻重叠的列车

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 区间占用表 `IntervalOccupancy` | `map<(站A,站B), set<TrainInterval>>` |
| P0 | **运行图冲突检测算法**：逐区间检查时间重叠 + 5 分钟裕量 | `detectConflict()` |
| P0 | 新增列车：校验 → 冲突检测 → 写入占用表 | `POST /api/admin/trains` |
| P0 | 删除列车：检查无未出发已售车票 → 清理占用表 | `DELETE /api/admin/trains/{id}` |
| P0 | 调整时刻：清旧占用 → 冲突检测 → 写新占用 | `PUT /api/admin/trains/{id}/schedule` |
| P1 | 临客有效期管理（过期自动停运） | `TrainStatus` 状态机 |

**可演示**：新增列车与已有列车时刻冲突 → 返回冲突详情（与哪个车次、哪个区间、何时冲突）。

---

### 第八阶段：审批流（预计 3 天）

**依赖**：第七阶段

**目标**：新增列车/调整时刻/新增线路/新增站点 均需审批

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 审批状态机：`DRAFT → SUBMITTED → APPROVED / REJECTED / EXPIRED` | `ApprovalRequest` 模型 |
| P0 | 提交审批 + 状态快照 | `POST /api/admin/trains` 返回审批 ID |
| P0 | **审批时二次冲突检测**（乐观并发控制） | 提交快照 vs 当前状态对比 |
| P0 | 四眼原则：提交人 ≠ 审批人（内置校验） | 不会出现自己审批自己 |
| P0 | CAS 审批锁：`atomic_flag` 防止多人同时审批 | 并发安全 |
| P1 | 72 小时超时自动驳回（Timer Wheel） | 定时器轮询 |
| P1 | 审批列表查询 + 审批/驳回接口 | `GET/POST /api/admin/approvals/*` |

**可演示**：职工 A 提交新增列车 → 职工 B 审批通过前，职工 C 修改了同一区间 → B 审批时检测到冲突，驳回并显示差异。

---

### 第九阶段：铁路网管理（预计 2 天）

**依赖**：第三阶段 + 第八阶段（审批流）

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 新增站点 | `POST /api/admin/stations`（走审批） |
| P0 | 新增线路 | `POST /api/admin/lines`（走审批） |
| P0 | 路网图更新后最短路径缓存失效 | Dijkstra 缓存标记脏 |
| P1 | 站点白名单校验（仅中文字符） | 输入安全 |
| P1 | 线路去重（端点相同拒绝） | 业务校验 |

---

### 第十阶段：管理员功能（预计 2 天）

**依赖**：第三阶段

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 用户管理：创建/修改角色/禁用/删除 | `/api/admin/users` CRUD |
| P0 | 系统配置：票价倍率、退票费率 | `GET/PUT /api/admin/config` |
| P1 | 否决已有审批结果（Admin 特权） | 审批链追加 |

---

### 第十一阶段：数据安全与稳定性（预计 4 天）

**依赖**：第五阶段（有写操作后才能验证 WAL）

**目标**：`kill -9` 后重启不丢数据，所有非功能需求就绪

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | **WAL 预写日志**：写操作先 append WAL + fsync → 改内存 | `WalWriter` |
| P0 | Checkpoint：每 5 分钟或 WAL ≥ 10MB 快照 | 快照 + 截断 WAL |
| P0 | 崩溃恢复：启动时重放 WAL | `kill -9` 后数据完整 |
| P0 | 优雅关闭：SIGINT/SIGTERM → 停止收请求 → 等待完成 → 刷 WAL → 退出 | 信号处理 |
| P0 | 线程池：`std::thread` + 无锁队列，大小 = `hardware_concurrency()` | 并发处理能力 |
| P1 | AES-256-GCM 加密敏感字段（身份证、手机号） | 数据静态加密 |
| P1 | HTTPS（TLS 1.3） | 传输加密 |
| P1 | 防重放：nonce + timestamp，已处理 nonce 缓存 5 分钟 | 重放攻击防护 |
| P1 | Token Bucket 限流：每 IP 60次查票/min、10次购票/min | 防刷票 |

**可演示**：购票过程中 `kill -9`，重启后数据完整，票数正确。

---

### 第十二阶段：审计系统 + 可视化 + 测试（预计 3 天）

**依赖**：第五阶段（有业务操作才能审计）、第九阶段（有路网才能导出）

| 优先级 | 任务 | 产出 |
|:---:|------|------|
| P0 | 审计日志写入（无锁队列：业务线程 push，logger 线程 pop 写盘） | `AuditLogger` |
| P0 | **链式校验**：每条 = `SHA256(自己内容 + 上条hash)` | 不可篡改 |
| P0 | 审计查询（按时间/操作类型/操作人过滤，含脱敏） | `GET /api/admin/audit?from=X&to=Y` |
| P1 | Graphviz DOT 导出 | `GET /api/admin/export/network/dot` |
| P1 | vis.js HTML 交互版导出（可选加分项） | `GET /api/admin/export/network/html` |
| P1 | 单元测试（核心模块覆盖率 > 80%） | Google Test |
| P1 | Thread Sanitizer + Address Sanitizer 通过 | `-fsanitize=thread -fsanitize=address` |
| P1 | Dockerfile + docker-compose | 一键部署 |

**可演示**：导出 DOT 生成 PNG 路网图；修改一条审计日志 → 整条链校验失败。

---

### 优先级总览

```
阶段 1: 项目骨架           ██░░░░░░░░░░  2天  无依赖
阶段 2: 数据模型+种子数据    ██░░░░░░░░░░  3天  依赖 1
阶段 3: 认证+RBAC          ██░░░░░░░░░░  3天  依赖 2
阶段 4: 旅客查票            ██░░░░░░░░░░  3天  依赖 3
阶段 5: 购票+并发控制       ███░░░░░░░░░  3天  依赖 4  ★ 核心亮点
阶段 6: 退票               █░░░░░░░░░░░  1天  依赖 5
阶段 7: 列车管理+冲突检测    ███░░░░░░░░░  4天  依赖 3  ★ 核心亮点
阶段 8: 审批流             ██░░░░░░░░░░  3天  依赖 7  ★ 核心亮点
阶段 9: 铁路网管理          █░░░░░░░░░░░  2天  依赖 3+8
阶段 10: 管理员功能         █░░░░░░░░░░░  2天  依赖 3
阶段 11: 数据安全+稳定性     ███░░░░░░░░░  4天  依赖 5  ★ 核心亮点
阶段 12: 审计+可视化+测试    ██░░░░░░░░░░  3天  依赖 5+9
                            ─────────
                            共约 33 天
```

> ⚠️ 阶段 5/7/8/11 是面试展示的核心亮点，时间分配上优先保障。
> 阶段 7 和 8 可与阶段 4-6 并行（职工端和旅客端独立，只要 RBAC 就绪即可）。

---

> 📅 创建日期：2026年7月
> 📎 关联文件：[[2027内蒙古秋招调研]] | [[2027届秋招时间规划与项目推荐]]
