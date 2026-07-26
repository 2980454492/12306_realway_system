// system_config.cpp — 系统配置实现
#include "core/system_config.h"
#include "core/logger.h"

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

    // 默认费率矩阵（元/km）——直观，无单位倍率
    //             商务   一等   二等   硬卧   硬座   无座
    // G 高铁
    rates_[0][0]=1.20; rates_[0][1]=0.80; rates_[0][2]=0.46; rates_[0][3]=0; rates_[0][4]=0;    rates_[0][5]=0.20;
    // D 动车
    rates_[1][0]=0.80; rates_[1][1]=0.50; rates_[1][2]=0.31; rates_[1][3]=0; rates_[1][4]=0;    rates_[1][5]=0.15;
    // C 城际
    rates_[2][0]=0.90; rates_[2][1]=0.55; rates_[2][2]=0.35; rates_[2][3]=0; rates_[2][4]=0;    rates_[2][5]=0.15;
    // Z 直达（普速，无商务/一等/二等）
    rates_[3][0]=0;    rates_[3][1]=0;    rates_[3][2]=0;    rates_[3][3]=0.30; rates_[3][4]=0.12; rates_[3][5]=0.08;
    // T 特快（普速，无商务/一等/二等）
    rates_[4][0]=0;    rates_[4][1]=0;    rates_[4][2]=0;    rates_[4][3]=0.25; rates_[4][4]=0.08; rates_[4][5]=0.06;
    // K 快速（普速，无商务/一等/二等）
    rates_[5][0]=0;    rates_[5][1]=0;    rates_[5][2]=0;    rates_[5][3]=0.22; rates_[5][4]=0.06; rates_[5][5]=0.05;
    // 其他（普速默认）
    rates_[6][0]=0;    rates_[6][1]=0;    rates_[6][2]=0;    rates_[6][3]=0.25; rates_[6][4]=0.08; rates_[6][5]=0.06;

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
    std::lock_guard<std::mutex> l(mutex_); return refund_rate_24h_;
}
double SystemConfig::refundRate2_24h() const {
    std::lock_guard<std::mutex> l(mutex_); return refund_rate_2_24h_;
}
double SystemConfig::refundRate2h() const {
    std::lock_guard<std::mutex> l(mutex_); return refund_rate_2h_;
}

// ── 写入 ──

void SystemConfig::setRate(char prefix, SeatType seat, double rate) {
    {
        std::lock_guard<std::mutex> l(mutex_);
        rates_[prefixIdx(prefix)][seatIdx(seat)] = rate;
    }
    save();
}

void SystemConfig::setRefundRates(double r24h, double r2_24h, double r2h) {
    {
        std::lock_guard<std::mutex> l(mutex_);
        refund_rate_24h_ = r24h; refund_rate_2_24h_ = r2_24h; refund_rate_2h_ = r2h;
    }
    save();
}

std::string SystemConfig::toJson() const {
    std::lock_guard<std::mutex> l(mutex_);
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

void SystemConfig::save() const {
    if (file_path_.empty()) return;
    try {
        std::ofstream out(file_path_);
        out << toJson();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save config: ") + e.what());
    }
}
