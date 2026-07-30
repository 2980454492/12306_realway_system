// line_service.h — 线路管理服务（CRUD），供 INFRA_ADMIN 路由调用
#pragma once

#include "models.h"
#include <vector>

namespace line_service {

std::vector<Line> getAll();
Line add(const Line& line);
bool update(uint32_t id, const Line& updated);
bool remove(uint32_t id);

}  // namespace line_service
