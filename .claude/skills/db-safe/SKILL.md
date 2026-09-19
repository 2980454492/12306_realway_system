---
name: db-safe
description: Rules for safely modifying the file-based data storage schema (JSON + WAL). Trigger when user wants to add an entity, add a field, or modify the data model.
---

## 数据存储变更安全规则

### 核心原则

1. **文件存储 = JSON 数据文件 + WAL 预写日志 + 定期快照**
2. **必须向前兼容**——旧版本的数据文件在新版本代码上必须能正常打开
3. **WAL 先行**——任何写操作先 append 到 WAL 文件（`fsync` 确保落盘），再修改内存数据
4. **崩溃恢复**——启动时重放 WAL 中未 checkpoint 的记录

### 存储架构

```
server/data/
  wal.log           # 预写日志（append-only，每条记录一行 JSON）
  snapshot.json     # 全量快照（定期生成）
  audit.log         # 审计日志（链式 SHA256）
  checkpoint.meta   # 记录已持久化的 WAL 偏移量
```

### 变更流程

#### 新增实体（如新增一个数据结构）

1. 定义 JSON schema（C++ struct + `to_json`/`from_json`）
2. 在快照文件中新增该实体的存储段：
   ```json
   {
     "stations": [...],
     "lines": [...],
     "trains": [...],
     "new_entity": []   // 新增字段，默认为空数组
   }
   ```
3. 实现该实体的 WAL 操作类型（如 `NEW_ENTITY_CREATE`、`NEW_ENTITY_DELETE`）
4. 启动时的 `loadSnapshot()` 兼容旧快照（缺少该 key 时默认空）
5. 崩溃恢复的 `replayWAL()` 能识别新的操作类型

#### 给已有实体新增字段

```cpp
// 旧结构体
struct Train {
    string id;
    string type;
    // ...
};

// 新结构体 — 新增字段必须给默认值
struct Train {
    string id;
    string type;
    string description = "";     // 新增字段，默认空字符串
    int max_speed_kmh = 0;       // 新增字段，默认 0
    // ...
};
```

**关键规则：**
- 新增字段必须有默认值（确保旧数据反序列化不失败）
- `nlohmann::json` 解析时用 `.value("new_field", default_value)` 而非 `["new_field"]`
- WAL 记录中新增字段同样给默认值

#### 删除/重命名字段

```cpp
// 不直接删除字段，用版本号标记废弃
struct Train {
    // ...
    int old_field_deprecated = 0;  // v2 废弃，v3 可删除
    int new_field = 0;
};
```

**规则：**
- 不直接删除已有字段（旧快照反序列化会丢数据）
- 废弃字段保留一个版本周期，下一版本再清理
- 重命名 = 新增 + 废弃旧字段

#### 修改 WAL 记录格式

WAL 每行一条 JSON 记录，格式为：

```json
{"op":"TRAIN_CREATE","ts":1718234567,"data":{...}}
{"op":"ORDER_CREATE","ts":1718234568,"data":{...}}
```

新增操作类型时：
- 确保 `replayWAL()` 中的 switch/match 能处理新类型
- 新类型对旧版本代码应是不可识别的（启动时跳过并警告，不 crash）

### 变更后的检查清单

- [ ] `loadSnapshot()` 能处理空数据文件（首次启动）
- [ ] `loadSnapshot()` 能处理旧版快照（缺少新字段不报错）
- [ ] `replayWAL()` 能跳过未知操作类型（不 crash）
- [ ] 新增字段有默认值，`from_json` 用 `.value(key, default)`
- [ ] 所有现有 API 功能正常（手动 curl 测试核心流程）
- [ ] `kill -9` 后重启不丢数据（WAL 崩溃恢复测试）
- [ ] 快照文件 + WAL 重放 = 完整状态（一致性验证）

### 禁止

- ❌ 直接修改 `snapshot.json` 的结构而不更新 `from_json` 兼容逻辑
- ❌ 删除已有字段而不保留默认值
- ❌ WAL 记录格式变更不做向后兼容
- ❌ 在业务代码中直接写磁盘（必须通过统一的 `WalWriter` 接口）
