// Auto-split from routes.cpp
#include "router_helpers.h"

void registerStaffRoutes(RailwayServer& server) {
    httplib::Server& app = server.getApp();

    /** GET /api/admin/trains — 获取列车列表（STAFF + APPROVER 均可访问） */
    app.Get("/api/admin/trains", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = RbacMiddleware::authenticate(
            req.has_header("Authorization") ? req.get_header_value("Authorization") : "");
        if (!ctx || (!RbacMiddleware::authorize(*ctx, Permission::MANAGE_TRAINS)
                  && !RbacMiddleware::authorize(*ctx, Permission::APPROVE))) {
            json j;
            j["ok"] = false;
            j["error"] = ctx ? "权限不足" : "未登录";
            res.set_content(j.dump(), "application/json");
            res.status = ctx ? 403 : 401;
            return;
        }

        const std::vector<Train>& trains = TrainManager::instance().getAllTrains();
        json arr = json::array();
        for (const Train& train : trains) {
            json jt;
            jt["id"] = train.id;
            jt["type"] = static_cast<int>(train.type);
            jt["status"] = static_cast<int>(train.status);
            jt["valid_from"] = train.valid_from;
            jt["valid_until"] = train.valid_until;
            jt["stops_count"] = train.stops.size();
            jt["stops"] = stopsToJson(train.stops, DataStore::instance());
            jt["segments"] = buildSegments(train, DataStore::instance());
            arr.push_back(jt);
        }

        json j;
        j["ok"] = true;
        j["count"] = arr.size();
        j["data"] = arr;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── POST /api/admin/trains — 新增列车（提交审批）──
    // ── PUT  /api/admin/trains/{id} — 修改列车（与新增共用逻辑，is_new 由 URL 是否有 id 决定）──
    auto handleTrainSubmit = [](const httplib::Request& req, httplib::Response& res, bool is_new) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        json body = json::parse(req.body);
        Train train = body.get<Train>();

        // 校验 + 冲突检测（提交/审批共用 checkTrain）
        TrainManager::CheckResult cr = TrainManager::instance().checkTrain(train, is_new);
        if (!cr.valid) {
            json j;
            j["ok"] = false;
            j["error"] = cr.error;
            if (!cr.conflicts.empty()) {
                json details = json::array();
                for (const TrainManager::ConflictDetail& c : cr.conflicts) {
                    json cd;
                    cd["train_id"] = c.train_id;
                    cd["station_a"] = c.station_a;
                    cd["station_b"] = c.station_b;
                    cd["line_id"] = c.line_id;
                    cd["conflicting_enter"] = c.conflicting_enter;
                    cd["conflicting_leave"] = c.conflicting_leave;
                    details.push_back(cd);
                }
                j["conflicts"] = details;
            }
            res.set_content(j.dump(), "application/json");
            res.status = cr.conflicts.empty() ? 400 : 409;
            return;
        }

        // 提交审批
        ApprovalType type = is_new ? ApprovalType::CREATE_TRAIN : ApprovalType::ADJUST_SCHEDULE;
        std::string payload;
        if (is_new) {
            // CREATE_TRAIN：立即写入 trains.json（PENDING），审批只存车次号
            train.status = TrainStatus::PENDING;
            DataStore& ds = DataStore::instance();
            // 已归档同名车次 → 先删除再新增
            ds.removeTrain(train.id);
            ds.addTrain(train);
            ds.saveTrains();
            WalWriter::instance().append("TRAIN_CREATE", json(train).dump());
            payload = json{{"train_id", train.id}}.dump();
        } else {
            // ADJUST_SCHEDULE：payload 存完整 stops（审批时需应用新数据）
            payload = body.dump();
        }
        std::string aid = ApprovalService::instance().submit(
            type, ctx->user_id, payload);

        json j;
        j["ok"] = true;
        j["approval_id"] = aid;
        j["message"] = "已提交审批";
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    };

    /** POST /api/admin/trains — 新增列车 */
    app.Post("/api/admin/trains", [handleTrainSubmit](const httplib::Request& req, httplib::Response& res) {
    handleTrainSubmit(req, res, true);
    });

    /** PUT /api/admin/trains/{id} — 修改列车 */
    app.Put(R"(/api/admin/trains/([^/]+))", [handleTrainSubmit](const httplib::Request& req, httplib::Response& res) {
    // 校验 URL 中的 train ID 与请求体一致，防止修改错列车
    try {
        json body = json::parse(req.body);
        std::string url_id = req.matches[1];
        std::string body_id = body.value("id", "");
        if (url_id != body_id) {
            json j;
            j["ok"] = false;
            j["error"] = "URL 与请求体中的车次号不一致";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }
    } catch (const std::exception&) {
        json j;
        j["ok"] = false;
        j["error"] = "请求体 JSON 格式错误";
        res.set_content(j.dump(), "application/json");
        res.status = 400;
        return;
    }
    handleTrainSubmit(req, res, false);
    });

    /** POST /api/admin/approvals/{id}/reject — 驳回审批申请 */
    app.Post(R"(/api/admin/approvals/([^/]+)/reject)", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::APPROVE);
        if (!ctx) return;

        std::string approval_id = req.matches[1];
        json body = json::parse(req.body);
        std::string comment = body.value("comment", "");
        if (comment.empty()) {
            json j;
            j["ok"] = false;
            j["error"] = "驳回时必须填写意见";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        ApprovalService::RejectResult result = ApprovalService::instance().reject(approval_id, ctx->user_id, comment);
        json j;
        j["ok"] = result.success;
        if (!result.success) j["error"] = result.error;
        else j["message"] = "已驳回";
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    /** GET /api/admin/stop-inserts — STAFF 查看待处理的线路变更 */
    app.Get("/api/admin/stop-inserts", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        // 线路变更：只返回 DRAFT（STAFF 提交后变为 SUBMITTED，不再显示，防止重复提交）
        std::vector<ApprovalRequest> drafts = ApprovalService::instance().getApprovals(ApprovalState::DRAFT);
        json arr = json::array();
        for (const ApprovalRequest& a : drafts) {
            if (a.type != ApprovalType::STOP_INSERT
                && a.type != ApprovalType::STOP_REPLACE
                && a.type != ApprovalType::STOP_REMOVE) continue;
            json ja;
            ja["id"] = a.id;
            ja["type"] = static_cast<int>(a.type);
            ja["submitter_id"] = a.submitter_id;
            ja["status"] = static_cast<int>(a.status);
            ja["submitted_at"] = a.submitted_at;
            try {
                ja["payload"] = json::parse(a.payload);
            } catch (...) {
                ja["payload"] = json();
            }
            arr.push_back(ja);
        }
        json j;
        j["ok"] = true;
        j["data"] = arr;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** PUT /api/admin/approvals/{id}/stop-time — STAFF 为线路加站审批填写停站时间 */
    app.Put(R"(/api/admin/approvals/([^/]+)/stop-time)", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        std::string approval_id = req.matches[1];
        json body = json::parse(req.body);
        int arr = body.value("arrival", 0);
        int dep = body.value("departure", 0);
        std::string effective_date = body.value("effective_date", "");
        if (arr <= 0 || dep <= 0) {
            badRequest(res, "请填写到达和发车时间");
            return;
        }
        // HHMM 格式校验：分钟部分须在 00-59
        if (arr % 100 >= 60 || dep % 100 >= 60) {
            badRequest(res, "时间格式错误（HHMM 分钟须在 00-59）");
            return;
        }

        ApprovalService::UpdateStopTimeResult result = ApprovalService::instance().updateStopTime(
            approval_id, arr, dep, effective_date, ctx->user_id);
        json j;
        j["ok"] = result.success;
        if (!result.success) j["error"] = result.error;
        else j["message"] = "停站时间已提交，等待审批";
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

}
