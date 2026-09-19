# 12306 铁路票务系统 — 项目开发规范

## 项目概述

C++17 铁路票务系统，模拟 12306 核心功能：查票、购票、退票、列车管理、审批流、RBAC 权限管控。
Drogon/Crow HTTP 框架，文件存储（JSON + WAL 预写日志），Docker 部署。
319 个站点 / 30 条线路 / 100 辆列车，单用户视角简化，面向秋招面试展示技术深度。

## 目录结构

```
server/src/          C++ 源码
  main.cpp            入口
  models.h            全局数据模型
  core/               HTTP 服务 + 日志 + 路由 + 工具函数（utils.h）
  data/               数据加载 + 铁路网图 + 种子生成
  auth/               认证 + JWT + RBAC
  passenger/          旅客端：查票 + 购票 + 退票
  staff/              铁路职工端（列车管理、审批）
  admin/              管理员端（用户管理、审计、配置）
server/config/       配置文件（站点、线路、列车种子数据 JSON）
server/data/         运行时数据目录（WAL 日志、快照文件、审计日志）
server/frontend/     前端静态文件（index.html / style.css / app.js）
scripts/             构建与启动脚本
docs/                设计文档（可选）
```

> 新增 .h/.cpp 必须放入对应模块子目录，**禁止**平铺在 `server/src/` 根下。移动文件时同步更新 include 路径和 CMakeLists.txt。

---

## C++ 规范

**文件组织**
- 一个 `.h` 配一个 `.cpp`，文件名 `snake_case`
- 头文件用 `#pragma once`
- include 顺序：自身 .h → 项目内 .h → 第三方库 → 标准库，组间空行
- **内部工具函数放 .cpp 匿名 namespace，不放 .h private**：不需要访问 `this` 的纯工具函数（如查找、转换、校验）→ `.cpp` 匿名 namespace。只有需要成员变量的才放 `.h` private。好处：改实现只重编一个 .cpp，签名变更不影响 includer
- **匿名 namespace 位置**：紧接在 `#include` 之后、所有函数之前。`namespace fs = std::filesystem;` 等别名放在匿名 namespace 之后

**命名**
| 类型 | 风格 | 示例 |
|------|------|------|
| 类/结构体 | PascalCase | `TrainManager`, `ApprovalService`, `RailwayGraph` |
| 函数 | camelCase | `queryTrains()`, `submitApproval()`, `detectConflict()` |
| 成员变量 | snake_case_ | `db_`, `mutex_`, `page_size_` |
| 局部变量/参数 | snake_case | `train_id`, `date_from` |
| 常量 | UPPER_SNAKE_CASE | `MAX_STATIONS`, `SAFETY_MARGIN_MINUTES` |

**错误处理**
- 文件/数据操作：返回 `bool` / `std::optional<T>` / `std::expected<T, Error>`(C++23)，不抛异常
- 参数校验失败：返回 400 + JSON 错误信息
- `std::stoi` / `std::stod` 必须 try-catch
- 致命错误：`std::cerr` + 优雅关闭(WAL flush) + `return 1`

**线程安全（关键）**
- 购票并发：每 (车次, 日期) 一个 `std::shared_mutex`。查票用 `shared_lock`（读），购票用 `unique_lock`（写）
- 审批并发：`std::atomic_flag` CAS 操作，第一个成功的获得审批权
- 数据存储并发：`std::shared_mutex` 保护 DataStore，读操作用 `shared_lock`，写操作用 `unique_lock`
- 运行图并发：`std::mutex` 保护 TrainManager 占用表，单写多读由 HTTP 事件循环串行化
- 审计日志并发：`std::mutex` + `std::condition_variable` 保护队列，业务线程 push，logger 线程阻塞 pop 写盘
- 死锁预防：两把及以上锁时，按车次 ID 字典序固定加锁顺序

**安全管理**
- 密码：argon2id 哈希（libsodium），独立 salt
- 敏感字段：AES-256-GCM 加密后存盘（身份证号、手机号）
- 输入校验：白名单验证（站名仅允许系统注册站名集合），防路径穿越
- JWT Token：RS256 签名，载荷含 userId、role、exp

**配置管理**
- 所有路径、端口、文件名统一在 `server/src/core/config.h` 的 `config` namespace 中定义
- 业务代码引用 `config::CONFIG_DIR`、`config::DEFAULT_PORT` 等常量，**禁止**硬编码字符串
- 新增可配置项时在 `config.h` 中添加，同步更新引用处
- Shell 脚本中用变量引用路径，通过环境变量覆盖默认值（如 `PORT=${PORT:-8080}`）

**代码格式**
- **一行一语句**：禁止用 `;` 在一行内写多个语句。变量声明例外：`int a = 0, b = 1;` 允许
- **if 语句**：单行体换行写，不加 `{}`；多行体必须加 `{}`
  ```cpp
  // 正确：单行换行，无花括号
  if (a > 0)
      doSomething();
  // 正确：多行有花括号
  if (a > 0) {
      doSomething();
      doMore();
  }
  // 错误：挤在一行
  if (a > 0) doSomething();
  // 错误：多行无花括号
  if (a > 0)
      doSomething();
      doMore();  // 缩进迷惑，实际上不在 if 内
  ```

**复用优先**
- 新增工具函数前，先用 `grep` 搜索项目是否已有相同实现。找到则直接复用
- 若现有函数仅在某个 `.cpp` 匿名 namespace 中，提升到 `utils.h` 供全局使用
- 此规则适用于：时间格式化、字符串处理、距离计算、JSON 序列化等所有通用工具
- **列车时刻变更（新增/调整/补站）校验复用**：所有涉及列车 stops 修改的操作，必须调用 `TrainManager::checkTrain()` 做时间合法性 + 冲突检测，**禁止**在业务代码中手动实现时间校验或冲突检测。速度校验额外叠加，用 `haversineDist()` + 限速 = `min(列车设计时速, 线路设计时速)` 检查

**禁止**
- ❌ `using namespace std;`（允许局部 `namespace fs = std::filesystem;`）
- ❌ `new`/`delete`（用 RAII、STL 容器、智能指针）
- ❌ 硬编码路径、端口、文件名（用 `config::` 常量或 shell 变量）
- ❌ 字符串拼接构造 JSON（用 `nlohmann::json`）
- ❌ 重复造轮子 —— 写新函数前不检查是否已有现成实现
- ❌ 一行多语句（`a=1; b=2;`）

---

## Shell 脚本规范

- **必须** `set -euo pipefail`
- 用颜色输出函数：`info()` `warn()` `error()`
- 环境工具检查：每一步操作检查返回值，失败给原因+解决办法
- 编译错误捕获到临时文件，失败时 grep 错误行展示
- 路径用变量，禁止硬编码

---

## CMake 规范

- CMakeLists.txt 在 `server/` 目录下
- **`build/` 目录必须在项目根目录**（`cmake -S server -B build`，勿放 `server/build/`）
- 用 `target_*` 命令（`target_include_directories`、`target_link_libraries`）
- 新增 .cpp 必须加入 `SOURCES` 列表
- 依赖库通过 `find_package` 或 `FetchContent` 引入
- 编译选项：`-Wall -Wextra -Wpedantic`

> ⚠️ **修改代码后不要自动执行编译或 build.sh**。用户自己负责构建和验证。改完代码直接说明改了什么即可。

---

## 前端规范

**技术栈**: 纯 HTML/CSS/JS SPA，零 npm 依赖，零框架。

### 结构分离原则

**静态结构放 HTML，动态数据放 JS。** 重复的卡片/列表项用 `<template>` 定义在 `index.html` 中，JS 只负责 `cloneNode` + 填数据，**禁止**在 JS 中用字符串拼接构造 HTML。

```html
<!-- index.html — 定义卡片骨架 -->
<template id="tpl-xxx-card">
  <div class="xxx-card">
    <span class="xxx-title"></span>
    <span class="xxx-meta"></span>
  </div>
</template>
```

```javascript
// app.js — 克隆 + 填数据，不拼 HTML 字符串
var tpl = U.$('tpl-xxx-card');
for (var i = 0; i < items.length; i++) {
  var card = tpl.content.cloneNode(true);
  card.querySelector('.xxx-title').textContent = items[i].title;
  card.querySelector('.xxx-meta').textContent = items[i].meta;
  listEl.appendChild(card);
}
```

**判断标准**：如果一个 DOM 结构在 HTML 中写死后不会变（只是数据不同），就写成 `<template>`。只有结构本身随数据变化（如条件渲染不同的子元素组合）时，才允许在 JS 中构建。

### 文件职责

| 文件 | 职责 |
|------|------|
| `index.html` | 页面骨架 + 所有 `<template>` + 静态 DOM |
| `style.css` | 全部样式，深色主题，CSS 变量统一定义色值 |
| `app.js` | 状态管理 + API 调用 + 模板克隆填充 + 事件绑定 |

### 缓存策略

静态文件用查询参数做版本戳：`/app.js?v=N`、`/style.css?v=N`。每次修改后递增版本号。

### 逻辑分层

**核心计算放后端，前端只做展示。** 能用后端 API 返回预计算结果的，不在前端重算。

| 场景 | 后端 | 前端 |
|------|:---:|:---:|
| 冲突检测 | ✅ 区间占用表 | — |
| Haversine 距离 / 时速 | ✅ 预计算存入 JSON | 直接取用 |
| 站名/线路名查表 | ✅ API 返回时填充 | 直接渲染 |
| 排序/筛选 | 小数据可前端 | 数据量大调后端 |
| 时间格式化 | HHMM 整数 | `fmtTime()` 显示 |

### 禁止

- ❌ JS 字符串拼接构造 HTML（用 template + cloneNode）
- ❌ `innerHTML` 写入用户数据（用 `textContent` 防 XSS）
- ❌ 引入任何 npm 依赖或 CDN 外部资源
- ❌ 硬编码颜色值在 JS 中（颜色统一在 CSS 中定义，JS 只切换 class）
- ❌ 前端重算后端已算好的数据

---

## 注释规范

> 详细审查清单见 `.claude/skills/comment-review/SKILL.md`

### 核心原则（三条）

1. **写"为什么"而非"是什么"** — 代码已经说了"是什么"，注释要回答"为什么选这个值 / 这个方案 / 这个顺序"
2. **一眼定位** — 打开任意文件 5 秒内能扫到：这文件干嘛的、分哪几块、关键决策在哪
3. **不腐烂** — 改代码同步改注释，注释掉的代码直接删掉（git 里有历史）

### 格式速查

| 场景 | 格式 | 示例 |
|------|------|------|
| 文件头 | `// 文件名 — 一句话职责` | `// train_manager.h — 列车增删改查与运行图冲突检测` |
| C++ 公开方法 | `/** 一句话说明 */` | `/** 查询两个站点之间的可用列车，支持直达和一次换乘 */` |
| 行内"为什么" | `// 原因` | `int margin = 5;  // 运行图冲突检测安全裕量(分钟)，行业标准` |
| 段落分隔（C++） | `// ── 段落名（中文）──` | `// ── 冲突检测 ──` |
| 段落分隔（Shell） | `# ── 步骤 N：说明 ──` | `# ── 步骤1：CMake 配置 ──` |

### 新增代码的底线

以下任一缺失视为**未完成**，不应提交：

- [ ] 新文件有文件头（`// xxx.xxx — 职责说明`）
- [ ] 新函数/方法有注释（用途 + 关键参数，一行够用不求 Doxygen 长文）
- [ ] 新类/结构体有注释（代表什么、谁在用）
- [ ] 关键设计决策有"为什么"注释（如：为什么用 shared_mutex 而非 mutex、为什么安全裕量是 5 分钟）
- [ ] 长文件有 `// ── xxx ──` 段落分隔

### 禁止

- ❌ 注释掉的代码（用 `git log` 回溯）
- ❌ 重复代码的废话注释（`i++; // i 自增`）
- ❌ `/* */` 在函数体内（`//` 统一风格）

### 文档自动维护规则

**项目中的所有 `.md` 文件必须保持一致，任何代码变更都须同步更新关联文档。**

#### 受维护的文档清单

| 文件 | 职责 |
|------|------|
| `README.md` | 项目概览、结构图、环境要求、API 表 |
| `MANUAL.md` | 功能需求清单 |
| `.claude/CLAUDE.md` | 编码规范 |
| `.claude/skills/*/SKILL.md` | 各 skill 的审查/流程规则 |

#### 具体同步规则

| 变更类型 | 必须同步的文档 |
|---------|--------------|
| 新增/删除/重命名目录或文件（含脚本） | `README.md`（项目结构图） |
| 新增/修改 C++ 源文件 | `server/CMakeLists.txt` + `README.md`（src 树） |
| 新增/修改/删除 API 端点 | `README.md`（API 表格）+ `MANUAL.md`（功能清单） |
| 修改编码规范 | `.claude/skills/code-review/SKILL.md` |
| 新增/删除 skill | `README.md`（.claude 节）+ `.claude/settings.json` |
| 修改数据存储 schema | `README.md` + `.claude/skills/db-safe/SKILL.md` |
| 修改构建依赖/环境要求 | `README.md`（环境要求）+ `MANUAL.md` |
| 修改端口号 | `README.md` + `scripts/run.sh` |
| 新增/修改前端功能（排序、筛选、展示字段等） | `MANUAL.md`（对应功能描述行） |

> ⚠️ **新功能文档规则**：任何后期新增的功能（前后端均适用），必须在 `MANUAL.md` 中找到对应的功能描述行并更新。如果该功能在需求文档中无对应条目，须新增条目描述。

**此规则无例外。** 修改代码时，第一步确认哪些文档需要更新，第二步改代码同时改文档，第三步验证文档间无矛盾。

每次代码变更完成后，**主动执行 doc-sync 检查**（详见 `.claude/skills/doc-sync/SKILL.md`），不得等用户提醒。用户说"继续"或开始新任务前，先确认上一轮变更的文档是否已同步。

- 提交信息：`【类型】描述`（类型：新增/修改/修复/优化/重构/文档）
- 一个提交只做一件事
- 不提交构建产物（`build/` `dist/`）
