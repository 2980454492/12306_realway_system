// system_config.cpp — 系统配置实现
#include "system_config.h"
#include "system/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

/** 列车类型前缀：G,D,C,Z,T,K,其他(7个) */
static const char PREFIXES[] = {'G','D','C','Z','T','K','*'};

/** 席位类型映射表：枚举值到 JSON 键名 (6个) */
static const std::pair<SeatType, const char*> SEATS[] = {
    {SeatType::BUSINESS,     "BUSINESS"},
    {SeatType::FIRST,        "FIRST"},
    {SeatType::SECOND,       "SECOND"},
    {SeatType::HARD_SLEEPER, "HARD_SLEEPER"},
    {SeatType::HARD_SEAT,    "HARD_SEAT"},
    {SeatType::NO_SEAT,      "NO_SEAT"},
};

int SystemConfig::seatIdx(SeatType s) { return static_cast<int>(s); }

int SystemConfig::prefixIdx(char p) {
    switch (p) {
        case 'G': return 0; case 'D': return 1; case 'C': return 2;
        case 'Z': return 3; case 'T': return 4; case 'K': return 5;
        default:  return 6;
    }
}

char SystemConfig::prefixChar(int idx) { return PREFIXES[idx]; }

// ── 单例 ──

SystemConfig& SystemConfig::instance() {
    static SystemConfig cfg;
    return cfg;
}

// ── 初始化 ──

bool SystemConfig::initialize(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_path_ = path;

    if (!fs::exists(path)) {
        save();
        Logger::instance().info("Created default system config: " + path);
        return true;
    }

    try {
        std::ifstream in(path);
        json j;
        in >> j;

        if (j.contains("rates")) {
            for (const auto& [prefix_key, seat_obj] : j["rates"].items()) {
                if (prefix_key.size() != 1) continue;
                int pi = prefixIdx(prefix_key[0]);
                for (int si = 0; si < 6; ++si) {
                    auto it = seat_obj.find(SEATS[si].second);
                    if (it != seat_obj.end())
                        rates_[pi][si] = it->get<double>();
                }
            }
        }

        refund_rate_24h_   = j.value("refund_rate_24h", 0.95);
        refund_rate_2_24h_ = j.value("refund_rate_2_24h", 0.90);
        refund_rate_2h_    = j.value("refund_rate_2h", 0.80);

        Logger::instance().info("Loaded system config from " + path);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to load config, using defaults: ") + e.what());
        save();
        return true;
    }
}

// ── 查询 ──

double SystemConfig::ratePerKm(const std::string& train_id, SeatType seat) const {
    std::lock_guard<std::mutex> l(mutex_);
    int pi = prefixIdx(train_id.empty() ? '*' : train_id[0]);
    int si = seatIdx(seat);
    return rates_[pi][si];
}

double SystemConfig::refundRate24h() const {
    std::lock_guard<std::mutex> l(mutex_); 
    return refund_rate_24h_;
}
double SystemConfig::refundRate2_24h() const {
    std::lock_guard<std::mutex> l(mutex_); 
    return refund_rate_2_24h_;
}
double SystemConfig::refundRate2h() const {
    std::lock_guard<std::mutex> l(mutex_); 
    return refund_rate_2h_;
}

// ── 序列化（调用者须持有 mutex_）──

std::string SystemConfig::toJsonLocked() const {
    json j;

    json rates_obj = json::object();
    for (int pi = 0; pi < 7; ++pi) {
        std::string key(1, PREFIXES[pi]);
        json seat_obj = json::object();
        for (int si = 0; si < 6; ++si)
            seat_obj[SEATS[si].second] = rates_[pi][si];
        rates_obj[key] = seat_obj;
    }
    j["rates"] = rates_obj;
    j["refund_rate_24h"]   = refund_rate_24h_;
    j["refund_rate_2_24h"] = refund_rate_2_24h_;
    j["refund_rate_2h"]    = refund_rate_2h_;
    return j.dump(2);
}

std::string SystemConfig::toJson() const {
    std::lock_guard<std::mutex> l(mutex_);
    return toJsonLocked();
}

// ── 写入 ──

void SystemConfig::setRate(char prefix, SeatType seat, double rate) {
    std::lock_guard<std::mutex> l(mutex_);
    rates_[prefixIdx(prefix)][seatIdx(seat)] = rate;
    if (!file_path_.empty()) {
        try {
            std::ofstream out(file_path_);
            out << toJsonLocked();
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("Failed to save config: ") + e.what());
        }
    }
}

void SystemConfig::setRefundRates(double r24h, double r2_24h, double r2h) {
    std::lock_guard<std::mutex> l(mutex_);
    refund_rate_24h_ = r24h; refund_rate_2_24h_ = r2_24h; refund_rate_2h_ = r2h;
    if (!file_path_.empty()) {
        try {
            std::ofstream out(file_path_);
            out << toJsonLocked();
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("Failed to save config: ") + e.what());
        }
    }
}

void SystemConfig::save() const {
    std::lock_guard<std::mutex> l(mutex_);
    if (file_path_.empty()) return;
    try {
        std::ofstream out(file_path_);
        out << toJsonLocked();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save config: ") + e.what());
    }
}
