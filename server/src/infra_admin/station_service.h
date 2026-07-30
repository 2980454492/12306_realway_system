// station_service.h — 站点管理服务（CRUD），供 INFRA_ADMIN 路由调用
#pragma once

#include "models.h"
#include <vector>

namespace station_service {

std::vector<Station> getAll();
Station add(const Station& station);
bool update(uint32_t id, const Station& updated);
bool remove(uint32_t id);

}  // namespace station_service
