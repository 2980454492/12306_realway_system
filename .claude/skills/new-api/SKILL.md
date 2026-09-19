---
name: new-api
description: Guide for adding a new REST API endpoint to the 12306 railway server. Trigger when user wants to add a new route, create a new REST endpoint, or extend the HTTP API.
---

## 新增 API 端点完整流程

### 1. 确定端点归属

| 前缀 | 角色 | 权限级别 |
|------|------|:---:|
| `/api/auth/*` | 认证相关 | 无需鉴权 |
| `/api/trains/*` | 旅客查票 | Passenger+ |
| `/api/orders/*` | 旅客购票/退票 | Passenger+（退票仅限自己） |
| `/api/admin/*` | 职工/管理员操作 | Staff+ / Admin |

### 2. 声明数据接口（对应的 manager 头文件）

在对应的 manager 类中声明方法：

```cpp
// train_manager.h
/** 查询列车经停站序列 */
std::optional<std::vector<Stop>> getTrainStops(const std::string& train_id);

// order_manager.h
/** 创建订单，返回订单 UUID。失败返回 nullopt */
std::optional<std::string> createOrder(const CreateOrderRequest& req);
```

### 3. 实现数据接口

- 文件存储：通过统一的 `DataStore` 接口读写
- 写操作须走 WAL：先 `walWriter.append(op, data)` → fsync → 修改内存
- 返回 `std::optional<T>` — 失败返回 `std::nullopt`
- 查询操作：优先查内存缓存，miss 则从快照加载

### 4. 注册路由（`server/src/routes.cpp`）

**Drogon 框架：**

```cpp
// GET /api/trains/{id}/stops — 查询列车经停站
app().registerHandler("/api/trains/{id}/stops",
    [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        // 1. 鉴权中间件（自动从 Authorization header 提取 JWT）
        auto auth = AuthMiddleware::verify(req);
        if (!auth.ok) {
            return callback(auth.errorResponse());
        }

        // 2. 解析参数
        auto train_id = req->getParameter("id");

        // 3. 调用业务逻辑
        auto stops = TrainManager::instance().getTrainStops(train_id);
        if (!stops) {
            return callback(HttpResponse::newHttpResponse(k404NotFound));
        }

        // 4. 构造 JSON 响应
        Json::Value json;
        json["ok"] = true;
        json["data"] = serializeStops(*stops);
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    });
```

**Crow 框架：**

```cpp
// GET /api/trains/<string:id>/stops
CROW_ROUTE(app, "/api/trains/<string:id>/stops")
    .methods("GET"_method)
    ([](const crow::request& req, std::string id) {
        // 1. 鉴权
        auto auth = AuthMiddleware::verify(req);
        if (!auth.ok) return auth.errorResponse();

        // 2-4. 同上
    });
```

### 5. RBAC 权限检查模板

```cpp
// 中间件层统一鉴权，按角色+权限位图检查
struct AuthResult {
    bool ok;
    std::string user_id;
    Role role;          // Passenger / Staff / Admin
    std::bitset<64> permissions;
};

// 在路由中使用
auto auth = AuthMiddleware::verify(req);
if (!auth.ok) {
    // 返回 401 Unauthorized
}
if (!auth.hasPermission(Permission::CREATE_TRAIN)) {
    // 返回 403 Forbidden
}
```

### 6. 注册到 CMake

如果新增了 `.cpp` 文件，加入 `server/CMakeLists.txt` 的 `SOURCES` 列表。

### 7. 文档同步

- [ ] 在 `README.md` 的 API 表格中添加新端点
- [ ] 在 `requirtment.md` 中更新功能清单（如涉及新功能）
- [ ] 更新审计日志的操作类型枚举（如涉及敏感操作）

### 验证

```bash
# 1. 构建
cd server/build && cmake --build . -j$(nproc)

# 2. 启动服务
./railway_server &

# 3. 先登录获取 Token
TOKEN=$(curl -s -X POST http://127.0.0.1:8080/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' | jq -r '.token')

# 4. 测试新接口
curl -s http://127.0.0.1:8080/api/xxx \
  -H "Authorization: Bearer $TOKEN" | jq

# 5. 测试权限拦截（用低权限 Token 调用高权限接口）
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/api/admin/xxx \
  -H "Authorization: Bearer $PASSENGER_TOKEN"
# 期望返回 403

# 6. 验证审计日志记录了本次操作
cat server/data/audit.log | grep "XXX" | tail -1 | jq
```
