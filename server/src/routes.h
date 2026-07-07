// routes.h — 路由注册，所有 HTTP 端点集中定义
#pragma once

class RailwayServer;

/**
 * 注册全部 HTTP 路由到 RailwayServer。
 * 新增端点只需在此函数内添加 handler。
 */
void registerRoutes(RailwayServer& server);
