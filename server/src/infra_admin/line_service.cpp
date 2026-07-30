// line_service.cpp — 线路管理实现（委托 DataStore）
#include "infra_admin/line_service.h"
#include "data/data_store.h"

namespace line_service {

std::vector<Line> getAll() {
    return DataStore::instance().getAllLines();
}

Line add(const Line& line) {
    return DataStore::instance().addLine(line);
}

bool update(uint32_t id, const Line& updated) {
    return DataStore::instance().updateLine(id, updated);
}

bool remove(uint32_t id) {
    return DataStore::instance().removeLine(id);
}

}  // namespace line_service
