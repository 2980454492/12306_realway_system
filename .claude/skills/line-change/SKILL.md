# 线路变更（Line Change）— 完整流程与代码规范

## 概述

线路变更是 INFRA_ADMIN 修改线路站点列表后，对受影响列车的停站进行同步变更的审批流程。
分为三种操作：**加站**、**改站**、**删站**。

## 状态机

```
DRAFT ──(STAFF填写提交)──→ SUBMITTED ──(APPROVER审批)──→ APPROVED
                                    └──(APPROVER驳回)──→ REJECTED
```

| 状态 | 含义 |
|------|------|
| `DRAFT` | INFRA_ADMIN 已修改线路，审批已创建，等待 STAFF 填写变更信息 |
| `SUBMITTED` | STAFF 已填写并提交，等待 APPROVER 审批 |
| `APPROVED` | APPROVER 已通过，变更已应用到列车 |
| `REJECTED` | APPROVER 已驳回 |

## 三种操作

### 1. 线路加站（STOP_INSERT）

- **触发**：线路上新增一个站点
- **STAFF 填写**：生效日期 + 到站/发车时间（或勾选"通过"）
- **审批通过后**：在受影响的列车 stops 中插入新停站

### 2. 线路改站（STOP_REPLACE）

- **触发**：线路上将 A 站替换为 B 站（如"长沙"→"长沙南"）
- **STAFF 填写**：生效日期 + 到站/发车时间（或勾选"通过"）
- **校验差异**：构建临时列车时，先移除旧站 A，再插入新站 B，然后做时间/速度/冲突校验
- **审批通过后**：在受影响的列车 stops 中替换旧站为新站

### 3. 线路删站（STOP_REMOVE）

- **触发**：线路上移除一个站点（无替换）
- **STAFF 填写**：生效日期（无需时间）
- **审批通过后**：从受影响的列车 stops 中移除该站

## 后端辅助函数

`approval_service.cpp` 中新增：

```cpp
// 根据线路变更类型构建审批 payload
json buildLineChangePayload(const std::string& action,  // "insert" | "replace" | "remove"
                            const Train& train, uint32_t line_id,
                            const std::string& station_name,
                            const std::string& old_station_name = "");
```

- `action="insert"`：创建 STOP_INSERT，status=DRAFT
- `action="replace"`：创建 STOP_REPLACE，status=DRAFT，payload 含 `replace_station_name`
- `action="remove"`：创建 STOP_REMOVE，status=DRAFT

## 关键约束

- 生效日期须 ≥ `MAX_ADVANCE_DAYS + 1`（15 天）
- 加站/改站须通过 `checkTrain()` 的时间 + 速度 + 冲突校验
- 改站校验时**必须先用旧站替换后的 stops 做校验**
- 前端的 `_resolveApprovalPayload` 须包含 STOP_REPLACE（type 7）的列车数据查找

## 涉及文件

| 文件 | 职责 |
|------|------|
| `models.h` | ApprovalType 加 STOP_REPLACE=7，ApprovalState 加 DRAFT=4 |
| `approval_service.h/cpp` | buildLineChangePayload()、updateStopTime()、approve() |
| `router_infra_admin.cpp` | PUT /api/admin/lines 调用辅助函数 |
| `router_staff.cpp` | /api/admin/stop-inserts 返回三种类型；submit 接口 |
| `router_approver.cpp` | 审批列表展示三种类型 |
| `staff.js` | 前端渲染、提交 |
| `index.html` | 模板 |
