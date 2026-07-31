// station_service.cpp — 站点管理实现（委托 DataStore）
#include "infra_admin/station_service.h"
#include "data/data_store.h"

namespace station_service {

const std::vector<Station>& getAll() {
    return DataStore::instance().getAllStations();
}

Station add(const Station& station) {
    return DataStore::instance().addStation(station);
}

bool update(uint32_t id, const Station& updated) {
    return DataStore::instance().updateStation(id, updated);
}

bool remove(uint32_t id) {
    return DataStore::instance().removeStation(id);
}

}  // namespace station_service
