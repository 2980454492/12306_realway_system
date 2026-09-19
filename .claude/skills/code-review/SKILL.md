---
name: code-review
description: Review C++ / Shell / CMake changes in the 12306 railway project for bugs, security, and style violations. Trigger when user asks for code review, pre-commit check, or "check my changes".
---

## 审查流程

**两步走，不可跳过第二步：**

1. **逐文件审查 git diff** — 按下方清单逐项检查每个变更文件
2. **全仓库结构扫描** — 不管 diff，扫描全部源文件：
   - 全部 `.h`：查不需要 `this` 的 `static`/`private` 方法、DRY 违规、include 顺序
   - 全部 `.cpp`：查一行多语句、if 单行体多余 `{}`、if 多行体缺失 `{}`
   - 全部 `.js`：查字符串拼 HTML、重复函数、前端重算后端数据、全局变量泄漏
   - 全部 `.html`：查 `<template>` 复用是否充分、静态结构是否误入 JS
   - 全部 `.css`：查 CSS 变量使用、颜色值是否硬编码在 JS 中

> ⚠️ 第二步是**强制**的。第一步只看到改动，第二步才能发现历史遗留问题和新改动引发的全局一致性问题。

## 审查清单

按 CLAUDE.md 编码规范逐项检查当前变更：

### C++ (`server/src/*.cpp`, `server/src/*.h`)

**代码规范：**
- [ ] 头文件用 `#pragma once`
- [ ] include 顺序正确：自身 .h → 项目内 .h → 第三方库 → 标准库，组间空行分隔。特别注意 `nlohmann/json.hpp` 属于第三方，不放标准库区域
- [ ] 禁止 `using namespace std;`（允许局部 `namespace fs = std::filesystem;`）
- [ ] 资源用 RAII：智能指针 / 栈对象 / STL 容器，禁止裸 `new`/`delete`
- [ ] 私有成员变量用 `snake_case_` 后缀
- [ ] 公开 API 有 `/** */` 注释。注意：全文件必须风格统一（全部 `/** */` 或全部 `//`，禁止混用）
- [ ] 私有方法/匿名 namespace 函数有 `//` 注释（至少一行说明用途）
- [ ] 类/结构体有 `/** */` 注释
- [ ] 常量有 `/** */` 注释
- [ ] 新增 .cpp 已加入 `server/CMakeLists.txt` 的 `SOURCES` 列表
- [ ] 每个文件有文件头注释（`// filename.xxx — 一句话职责`）
- [ ] 长文件有 `// ── 段落名 ──` 段落分隔符（每 ~5 个函数一组）
- [ ] 函数顺序与头文件声明顺序一致（或按类别分组：构造/初始化 → 查询 → 变更 → 私有工具）
- [ ] 不需要访问 `this` 的工具函数放 `.cpp` 匿名 namespace，不放 `.h` private
- [ ] **一行一语句**：禁止 `a=1; b=2;` 多语句挤一行（变量声明例外）
- [ ] **if 语句**：单行体换行不加 `{}`；多行体必须加 `{}`

**安全：**
- [ ] `std::stoi` / `std::stod` 必须 try-catch
- [ ] 输入校验：站名白名单验证，日期格式严格校验，防路径穿越
- [ ] 密码存储：argon2id 哈希（libsodium），不得明文或弱哈希
- [ ] 敏感字段（身份证号、手机号）AES-256-GCM 加密后存盘
- [ ] JWT Token 校验：每个受保护端点从 Authorization header 提取并验证
- [ ] RBAC 权限检查：中间件层完成认证+鉴权，业务逻辑不重复检查
- [ ] 输出脱敏：身份证→`37****199001010011`、手机号→`138****1234`
- [ ] 无硬编码密钥/密码/Token

**线程安全：**
- [ ] 购票路径：每 (车次, 日期, 席位) 用 `std::shared_mutex`，查票 shared_lock，购票 unique_lock
- [ ] 审批路径：`std::atomic_flag` CAS 防止多人同时审批同一申请
- [ ] 运行图读写：`std::shared_mutex` 保护，读多写少
- [ ] 审计日志写入：无锁队列（业务线程 push，logger 线程 pop）
- [ ] 多锁场景：按车次 ID 字典序固定加锁顺序，防止死锁

**数据完整性：**
- [ ] WAL 写操作先 `append` 到 WAL 文件 + `fsync` 确保落盘
- [ ] Checkpoint：定期快照 + 截断已持久化的 WAL
- [ ] 优雅关闭：SIGINT/SIGTERM 信号处理 → 停止接收新请求 → 等待进行中请求 → 刷 WAL → 退出
- [ ] 审计日志链式校验：每条记录 = `SHA256(自己hash + 上一条hash)`

**业务逻辑：**
- [ ] 购票原子性：同车次同日期同席位不超卖
- [ ] 退票费率：发车前>24h 退95%、2-24h 退90%、<2h 退80%、发车后不可退
- [ ] 运行图冲突检测：每个区间 `max(新增进入, 已有进入) < min(新增离开, 已有离开) + 5分钟裕量`
- [ ] 审批流：四眼原则（提交人 ≠ 审批人），审批时二次冲突校验
- [ ] 新增列车/线路/站点：需审批通过方可生效
- [ ] 删除列车：检查无未出发已售车票方可删除

### Shell (`scripts/*.sh`)

- [ ] 脚本开头有 `set -euo pipefail`
- [ ] 每一步操作检查返回值，失败给出原因和解决办法
- [ ] 路径用变量，禁止硬编码 `/home/xxx`
- [ ] 用 `[[ ]]` 做条件判断
- [ ] 输出用 `info()` / `warn()` / `error()` 颜色函数

### CMake (`server/CMakeLists.txt`)

- [ ] 用 `target_*` 命令
- [ ] 新增源文件已加入 `SOURCES` 列表
- [ ] C++17 标准已设置：`set(CMAKE_CXX_STANDARD 17)`
- [ ] 编译选项：`-Wall -Wextra -Wpedantic`
- [ ] 依赖库正确链接（Drogon/Crow、libsodium、OpenSSL、nlohmann/json）

### 前端 (`server/frontend/*.js`, `server/frontend/*.html`, `server/frontend/*.css`)

**格式（与 C++ 同规则）：**
- [ ] **一行一语句**：禁止 `a=1; b=2;` 多语句挤一行（变量声明 `var a=0,b=1;` 例外）
- [ ] **if 单行体换行不加 `{}`**：禁止 `if (x) { stmt; }` 和 `if (x) stmt;`（单行体须独占一行）
- [ ] **if 多行体换行加 `{}`**：禁止 `if (x) { stmt1; stmt2; }`（多条语句挤在同行花括号内）
- [ ] **else 同行同理**：禁止 `} else { stmt1; stmt2; }`，else 体换行写，单行不加 `{}`、多行加 `{}`
- [ ] 审查时必须用脚本自动扫描：`grep -n ';.*;' app.js`（排除 for 头），逐行确认拆分

**结构分离（模板 vs 字符串拼接）：**
- [ ] 重复的卡片/列表项用 `<template>` 定义在 HTML 中，JS 只用 `cloneNode` + `textContent` 填数据
- [ ] **禁止 JS 字符串拼接构造 HTML**：扫描 `html += '<div'`、`'<div class="' +` 等模式。例外：结构本身随数据条件变化（如换乘 vs 直达卡片结构不同），允许拼接，但须注释说明原因
- [ ] **禁止 `innerHTML` 写入用户数据**（用 `textContent` 防 XSS）。对后端返回的站名、车次号、乘车人姓名等，即使后端已转义，前端也必须用 `textContent`

**逻辑分层（后端算，前端取）：**
- [ ] **Haversine 距离**：后端 `buildSegments()` 从 stops 按需推导 `distance_km`，API 响应中携带，前端直接取用，**禁止**前端重算
- [ ] **运行时速**：同上，`speed_kmh` 由后端推导，前端直接取用，**禁止**前端重算 `(distance / minutes) * 60`
- [ ] **站名/线路名**：后端 API 返回时填充 `station_name` / `line_name` 字段，前端不反查 `State.stations` 数组
- [ ] **排序/筛选**：小数据（≤ 200 条）可前端排序，大数据须调后端 API

**DRY（禁止重复定义）：**
- [ ] 同一映射表（如 `statusLabel`、`typeLabel`、`SEAT_MAP`）不得在多个函数中重复定义，提取为模块级常量或共享函数
- [ ] 站名按 ID 反查模式（`for (var i=0; i<State.stations.length; i++) { if (stations[i].id === id) ... }`）不得在多处内联展开，统一通过 `_stationName(id)` 等工具函数调用
- [ ] datalist `<option>` 构建逻辑不得在多个函数中重复
- [ ] 相同的筛选 checkbox 组（如车型选择）不得在 HTML 中多页各写一份，用模板或 `<datalist>` 复用

**代码组织：**
- [ ] 无全局变量泄漏（`var SEAT_MAP` 之类挂在 window 上），应挂到已有模块对象（`UI`/`U`/`Auth`/`State`）下
- [ ] 单函数 ≤ 80 行为宜，过长的拆分子函数。典型信号：3 层以上嵌套、一个函数做"渲染 + 排序 + 筛选"三件事
- [ ] `<template>` 不使用的隐藏 `<div>` 应移除或标注用途
- [ ] CSS 颜色值统一用 CSS 变量，JS 中不硬编码 `#e94560` 等色值（JS 只切换 class）

**安全：**
- [ ] 用户输入/后端返回的用户数据写入 DOM 时，走 `textContent`，不走 `innerHTML`
- [ ] 无硬编码 Token/密钥/密码



#### 目录结构

**详见 `CLAUDE.md` 的"目录结构"章节。** 此处在审查时检查：

- [ ] 新增 .h/.cpp 是否放入对应模块子目录（参考 `CLAUDE.md` 目录树）而非平铺在 `server/src/` 根下
- [ ] 跨模块共享的类型/函数是否放入 `models.h` 或 `geo_utils.h`
- [ ] 移动/新增文件后 `CMakeLists.txt` 的 `SOURCES`/`HEADERS` 和 `#include` 路径是否同步更新

#### 重构检查

- [ ] **DRY**：相同的函数/常量/公式不得在多个文件中重复定义。提取到共享头文件（函数→`xxx_utils.h`，常量→一处定义多处引用）
- [ ] **写前检查**：新增工具函数前，先用 `grep` 搜索项目是否已有相同功能的实现。找到则直接复用；若现有函数在某个 `.cpp` 匿名 namespace 中，将其提升到 `utils.h` 供全局使用
- [ ] **函数内重复检验**：同一函数内不得对同一数据做两次相同的查找/校验。如已找到了 from_idx/to_idx，后续不能再重建数组重新搜索同一对值；空数组时直接复用已有下标
- [ ] **参数精简**：函数签名中已无实际用途的参数须删除（含所有调用方），不要留 `/*unused*/` 占位
- [ ] **单例引用复用**：同一函数内多次用到同一单例时，取一次存为局部引用，勿每处都调 `Xxx::instance()`
- [ ] **数据完整性**：查询结果的字段要覆盖全部数据源（如换乘方案须同时包含两段列车的停站信息）
- [ ] **禁止魔法数字**：任何有语义的数值（天数、费率、超时、裕量等）必须定义为具名常量，禁止裸数字出现在逻辑中。C++ → `inline constexpr int NAME = N;`，JS → 模块级 `const NAME = N;` 或对象属性。例外：循环初始值 `i=0`、数组索引、字符串长度等无独立语义的值
- [ ] **硬编码收敛**：同一语义的数值/路径/端口只在一处定义，其他位置通过常量引用

- [ ] **不变数据持久化**：启动时计算但运行中不变的数据，须缓存到本地 JSON 文件。首次构建 → 写文件，后续启动 → 读文件。已缓存的数据：`data/orders.json`、`data/station_train_index.json`、`data/railway_graph.json`。新增同类数据（如站点距离矩阵、城市-站点索引）时遵循相同模式。**例外**：计算成本极低的数据（如单次 `haversineDist`，微秒级）不缓存，现场计算比读文件更快

- [ ] **禁止 O(n) 线性查找**：按名称/ID/城市等键值查找实体时，**必须使用预建索引**（`std::unordered_map`），禁止 `for` 循环遍历 `getAllStations()`/`getAllLines()`/`getAllTrains()` 等全量列表。DataStore 已提供以下 O(1) 索引：
  | 索引 | 方法 | 场景 |
  |------|------|------|
  | `station_index_` | `getStation(id)` | ID → 站 |
  | `name_to_id_` | `nameToStationId(name)` | 站名 → ID |
  | `city_to_ids_` | `cityToStationId(city)` / `getStationIdsByCity(city)` | 城市名 → ID（取首个）/ 全部 ID 列表 |
  | `station_name_set_` | `getStationNameSet().count(s)` | 站名/城市名合法性校验 |
  | `train_index_` | `getTrain(id)` | ID → 列车 |
  | `line_index_` | 待添加 | ID → 线路 |
  如需按其他键查找，先在 DataStore 中添加索引，再写业务逻辑。

### 通用

- [ ] 无硬编码绝对路径
- [ ] 无密码/Token/密钥泄露
- [ ] 致命错误输出到 `std::cerr` + 优雅关闭

---

## 输出格式

按严重程度排序：

| 级别 | 含义 | 示例 |
|------|------|------|
| 🔴 阻断 | 安全漏洞 / 会 crash / 数据丢失 | 无锁并发购票、WAL 未 fsync、SQL 注入、未捕获异常、空指针解引用 |
| 🟡 警告 | 违反编码规范 / 潜在风险 | 命名不规范、缺少注释、未用 RAII、缺少 RBAC 检查 |
| 🔵 建议 | 可读性 / 性能优化 | 代码简化、去重、缓存优化 |

每个问题附：**文件:行号** + **违规内容** + **修复建议**。
