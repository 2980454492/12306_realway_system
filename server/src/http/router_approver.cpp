// Auto-split from routes.cpp
#include "router_helpers.h"

void registerApprovalRoutes(RailwayServer& server) {
    auto& app = server.getApp();
    // ── DELETE /api/admin/trains/{id} — 删除列车（提交审批）──
    app.Delete(R"(/api/admin/trains/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
        if (!ctx) return;

        std::string train_id = req.matches[1];
        auto& ds = DataStore::instance();
        auto* train = ds.getTrain(train_id);
        if (!train) {
            json j;
            j["ok"] = false;
            j["error"] = "列车不存在";
            res.set_content(j.dump(), "application/json");
            res.status = 404;
            return;
        }

        std::string del_date = req.get_param_value("date");

        // 删除日期须 ≥15 天（14 天内的票已放出）
        if (!del_date.empty()) {
            if (!isFuture(del_date, 365)) {
                json j;
                j["ok"] = false;
                j["error"] = "日期不能是过去";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }
            if (isFuture(del_date, MAX_ADVANCE_DAYS)) {
                json j;
                j["ok"] = false;
                j["error"] = "删除日期须至少 15 天后（第14天已放票）";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }
        }

        json payload;
        payload["id"] = train_id;
        if (!del_date.empty()) payload["delete_date"] = del_date;
        std::string aid = ApprovalService::instance().submit(
            ApprovalType::DELETE_TRAIN, ctx->user_id, payload.dump());

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
    });

    // ── GET /api/admin/approvals — 审批列表（STAFF 看自己提交 / APPROVER 看所有+审批记录）──
    app.Get("/api/admin/approvals", [](const httplib::Request& req, httplib::Response& res) {
    try {
        // STAFF 和 APPROVER 均可访问（STAFF 只能看自己的提交）
        auto ctx = RbacMiddleware::authenticate(
            req.has_header("Authorization") ? req.get_header_value("Authorization") : "");
        if (!ctx || (!RbacMiddleware::authorize(*ctx, Permission::APPROVE)
                  && !RbacMiddleware::authorize(*ctx, Permission::MANAGE_TRAINS))) {
            json j;
            j["ok"] = false;
            j["error"] = ctx ? "Forbidden" : "Unauthorized";
            res.set_content(j.dump(), "application/json");
            res.status = ctx ? 403 : 401;
            return;
        }

        std::string status = req.get_param_value("status");
        std::optional<ApprovalState> filter;
        if (status == "SUBMITTED") filter = ApprovalState::SUBMITTED;
        else if (status == "APPROVED") filter = ApprovalState::APPROVED;
        else if (status == "REJECTED") filter = ApprovalState::REJECTED;
        else if (status == "WITHDRAWN") filter = ApprovalState::WITHDRAWN;

        std::string submitter_id = req.get_param_value("submitter_id");
        std::string approver_id = req.get_param_value("approver_id");

        // 如果传入的是用户名而非 UUID，解析为 user_id（前端可能只有 username）
        auto& auth = AuthService::instance();
        if (!submitter_id.empty()) {
            auto* u = auth.findUser(submitter_id);  // 先当 username 查
            if (u) submitter_id = u->id;             // 转为 UUID
        }
        if (!approver_id.empty()) {
            auto* u = auth.findUser(approver_id);
            if (u) approver_id = u->id;
        }

        auto approvals = ApprovalService::instance().getApprovals(filter);
        json arr = json::array();
        for (const auto& a : approvals) {
            if (!submitter_id.empty() && a.submitter_id != submitter_id) continue;
            if (!approver_id.empty() && a.approver_id != approver_id) continue;

            json ja;
            ja["id"] = a.id;
            ja["type"] = static_cast<int>(a.type);
            ja["submitter_id"] = a.submitter_id;
            ja["approver_id"] = a.approver_id;
            ja["status"] = static_cast<int>(a.status);
            ja["submitted_at"] = a.submitted_at;
            ja["decided_at"] = a.decided_at;
            ja["comment"] = a.comment;
            try { ja["payload"] = json::parse(a.payload); } catch (...) { ja["payload"] = json(); }
            // 为 CREATE_TRAIN / ADJUST_SCHEDULE 补齐 segments（前端展示里程/速度用）
            if ((a.type == ApprovalType::CREATE_TRAIN || a.type == ApprovalType::ADJUST_SCHEDULE)
                && ja["payload"].contains("stops")) {
                Train temp;
                temp.stops = ja["payload"]["stops"].get<std::vector<Stop>>();
                ja["payload"]["segments"] = buildSegments(temp, DataStore::instance());
            }
            arr.push_back(ja);
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

    // ── POST /api/admin/approvals/{id}/approve — 审批通过 ──
    app.Post(R"(/api/admin/approvals/([^/]+)/approve)", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::APPROVE);
        if (!ctx) return;

        std::string approval_id = req.matches[1];
        auto result = ApprovalService::instance().approve(approval_id, ctx->user_id);
        json j;
        j["ok"] = result.success;
        if (!result.success) {
            j["error"] = result.error;
            res.status = 400;
        } else {
            j["train_id"] = result.train_id;
            j["message"] = "审批通过，列车已生效";
        }
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
