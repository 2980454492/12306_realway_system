---
name: doc-sync
description: Scan code changes and conversation context, then automatically update affected .md documentation files. Invoke proactively after any feature/code change — the user should NOT have to ask.
---

## 触发时机（自动，用户不应需要主动调用）

以下任一情况发生时，**主动执行本 skill 的检查流程**，不需要等用户命令：

1. **代码变更完成后** — 新增/修改/删除了 C++ 源码、前端文件、Shell 脚本、CMake 配置
2. **功能讨论后** — 用户在对话中明确了新功能需求、设计决策、或优先级变更
3. **用户说"继续"之前** — 如果一个任务涉及多轮对话，在任务阶段性完成后检查
4. **/code-review 执行后** — 如果审查发现有影响文档的变更

> ⚠️ 当前问题是用户每次改完代码，`requirtment.md` 和 `README.md` 就过期了。本 skill 的目标是**消除"用户提醒才更新"的延迟**。

---

## 检查流程

### 步骤 1：扫描变更范围

```bash
# 检查 git diff 中有哪些文件变更
git diff --name-only HEAD

# 如果无变更（刚提交完），检查最近一个 commit
git diff --name-only HEAD~1
```

### 步骤 2：判断哪些文档受影响

对照下表逐项判断：

| 变更信号 | 必须更新的文档 |
|---------|--------------|
| C++ 新增/删除/重命名文件 | `README.md`（项目结构图） |
| 新增/修改/删除 API 端点 | `README.md`（API 表）+ `requirtment.md`（功能清单） |
| 新增前端功能（筛选、排序、新页面等） | `requirtment.md`（F-5 对应行） |
| 新增/修改数据结构字段 | `requirtment.md`（四、数据模型） |
| 新增业务逻辑规则（如费率、校验规则） | `requirtment.md`（二、功能需求） |
| 修改构建依赖/编译选项 | `README.md`（环境要求）+ `requirtment.md` |
| 修改端口/配置 | `README.md` + `scripts/` 相关脚本 |
| 新增/删除 skill | `README.md`（.claude 节）+ `.claude/settings.json` |
| 编码规范变更 | `.claude/skills/code-review/SKILL.md` |
| 修改数据存储 schema | `.claude/skills/db-safe/SKILL.md` |
| 对话中讨论但未记录的新需求/决策 | `requirtment.md`（对应章节） |

### 步骤 3：定位变更内容

- **C++ 变更** → 读 diff 中的函数签名、类定义、注释
- **前端变更** → 读 diff 中的 HTML/JS 结构变化
- **对话上下文** → 回溯本 session 中用户提出的新需求、设计决策
- **配置变更** → 读 config JSON 文件的变化

### 步骤 4：执行更新

逐一更新受影响的 `.md` 文件：

1. **`requirtment.md`**（功能需求清单）
   - 新增功能 → 在对应 F-x 章节新增/修改行
   - 新需求 → 在对应章节末尾新增条目
   - 已实现功能 → 勾选验收标准 checkbox
   - 数据结构变化 → 同步更新第四章数据模型

2. **`README.md`**（如果存在）
   - 目录结构变化 → 更新项目树
   - API 端点变化 → 更新 API 表格
   - 新增依赖 → 更新环境要求

3. **`.claude/skills/*/SKILL.md`**
   - 编码规范变化 → 同步 code-review 清单
   - 数据安全规则变化 → 同步 db-safe 规则

### 步骤 5：汇报

更新完成后，用一句话告知用户更新了哪些文件、哪些章节。

格式：`📄 已同步文档：requirtment.md（F-5.2 筛选功能）、README.md（API 表新增 /api/xxx）`

---

## 更新原则

- **增量更新，不重写**：在现有内容基础上追加/修正，不推倒重来
- **不编造**：只记录已经实现或已明确决定的内容，不猜测未来
- **与代码一致**：文档描述必须和当前代码的实际行为匹配
- **一个变更一次更新**：不要等用户提醒，改完代码就同步文档
