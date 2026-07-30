// Auto-split from routes.cpp
#include "core/routes_helpers.h"

void registerStaffRoutes(RailwayServer& server) {
    auto& app = server.getApp();
    // ── GET/PUT /api/admin/config — 系统配置（SYS_ADMIN）──
    app.Get("/api/admin/config", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::SYSTEM_CONFIG);
        if (!ctx) return;

        json j;
        j["ok"] = true;
        j["data"] = json::parse(SystemConfig::instance().toJson());
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    app.Put("/api/admin/config", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::SYSTEM_CONFIG);
        if (!ctx) return;

        json body = json::parse(req.body);
        auto& cfg = SystemConfig::instance();

        // 费率矩阵：{"G":{"BUSINESS":1.20,...},"D":{...},...}
        if (body.contains("rates")) {
            for (const auto& [prefix_key, seat_obj] : body["rates"].items()) {
                if (prefix_key.size() != 1) continue;
                char prefix = prefix_key[0];
                for (const auto& [seat_key, rate_val] : seat_obj.items()) {
                    SeatType st = nlohmann::json(seat_key).get<SeatType>();
                    cfg.setRate(prefix, st, rate_val.get<double>());
                }
            }
        }
        if (body.contains("refund_rate_24h") && body.contains("refund_rate_2_24h")
            && body.contains("refund_rate_2h")) {
            cfg.setRefundRates(
                body["refund_rate_24h"].get<double>(),
                body["refund_rate_2_24h"].get<double>(),
                body["refund_rate_2h"].get<double>());
        }

        AuditLogger::instance().log(ctx->user_id, ctx->role, "CONFIG_UPDATE",
            "system", body.dump(), "success");

        json j;
        j["ok"] = true;
        j["data"] = json::parse(cfg.toJson());
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── GET /api/admin/trains — 列车列表 ──
    app.Get("/api/admin/trains", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        auto& trains = TrainManager::instance().getAllTrains();
        json arr = json::array();
        for (const auto& train : trains) {
            json jt;
            jt["id"] = train.id;
            jt["type"] = static_cast<int>(train.type);
            jt["status"] = static_cast<int>(train.status);
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
        auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        json body = json::parse(req.body);
        Train train = body.get<Train>();

        // 校验 + 冲突检测（提交/审批共用 checkTrain）
        auto cr = TrainManager::instance().checkTrain(train, is_new);
        if (!cr.valid) {
            json j;
            j["ok"] = false;
            j["error"] = cr.error;
            if (!cr.conflicts.empty()) {
                json details = json::array();
                for (const auto& c : cr.conflicts) {
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
        auto type = is_new ? ApprovalType::CREATE_TRAIN : ApprovalType::ADJUST_SCHEDULE;
        std::string payload;
        if (is_new) {
            // CREATE_TRAIN：立即写入 trains.json（PENDING），审批只存车次号
            train.status = TrainStatus::PENDING;
            auto& ds = DataStore::instance();
            // 已归档同名车次 → 先删除再新增
            ds.removeTrain(train.id);
            ds.addTrain(train);
            ds.saveTrains();
            WalWriter::instance().append("TRAIN_CREATE", json(train).dump());
            payload = json{{"id", train.id}}.dump();
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

    app.Post("/api/admin/trains", [handleTrainSubmit](const httplib::Request& req, httplib::Response& res) {
    handleTrainSubmit(req, res, true);
    });

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

    // ── POST /api/admin/approvals/{id}/reject — 审批驳回 ──
    app.Post(R"(/api/admin/approvals/([^/]+)/reject)", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::APPROVE);
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

        auto result = ApprovalService::instance().reject(approval_id, ctx->user_id, comment);
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

}
