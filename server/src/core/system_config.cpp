// system_config.cpp — 系统配置实现
#include "core/system_config.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

SystemConfig& SystemConfig::instance() {
    static SystemConfig cfg;
    return cfg;
}

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
        base_rate_per_km_   = j.value("base_rate_per_km", 0.30);
        seat_rate_business_  = j.value("seat_rate_business", 3.0);
        seat_rate_first_     = j.value("seat_rate_first", 2.0);
        seat_rate_second_    = j.value("seat_rate_second", 1.0);
        seat_rate_hard_sleeper_ = j.value("seat_rate_hard_sleeper", 0.8);
        seat_rate_hard_seat_ = j.value("seat_rate_hard_seat", 0.4);
        seat_rate_no_seat_   = j.value("seat_rate_no_seat", 0.3);
        train_rate_g_ = j.value("train_rate_g", 1.50);
        train_rate_d_ = j.value("train_rate_d", 0.95);
        train_rate_c_ = j.value("train_rate_c", 1.10);
        train_rate_z_ = j.value("train_rate_z", 0.65);
        train_rate_t_ = j.value("train_rate_t", 0.50);
        train_rate_k_ = j.value("train_rate_k", 0.40);
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

// ── Getters ──

#define GETTER(name, field) \
    double SystemConfig::name() const { std::lock_guard<std::mutex> l(mutex_); return field; }

GETTER(baseRatePerKm,    base_rate_per_km_)
GETTER(seatRateBusiness,  seat_rate_business_)
GETTER(seatRateFirst,     seat_rate_first_)
GETTER(seatRateSecond,    seat_rate_second_)
GETTER(seatRateHardSleeper, seat_rate_hard_sleeper_)
GETTER(seatRateHardSeat,  seat_rate_hard_seat_)
GETTER(seatRateNoSeat,    seat_rate_no_seat_)
GETTER(trainRateG, train_rate_g_)
GETTER(trainRateD, train_rate_d_)
GETTER(trainRateC, train_rate_c_)
GETTER(trainRateZ, train_rate_z_)
GETTER(trainRateT, train_rate_t_)
GETTER(trainRateK, train_rate_k_)
GETTER(refundRate24h,   refund_rate_24h_)
GETTER(refundRate2_24h, refund_rate_2_24h_)
GETTER(refundRate2h,    refund_rate_2h_)

#undef GETTER

// ── Setters ──

void SystemConfig::setBaseRatePerKm(double rate) {
    { std::lock_guard<std::mutex> l(mutex_); base_rate_per_km_ = rate; }
    save();
}
void SystemConfig::setSeatRates(double business, double first, double second,
                                double hard_sleeper, double hard_seat, double no_seat) {
    { std::lock_guard<std::mutex> l(mutex_);
      seat_rate_business_ = business; seat_rate_first_ = first;
      seat_rate_second_ = second; seat_rate_hard_sleeper_ = hard_sleeper;
      seat_rate_hard_seat_ = hard_seat; seat_rate_no_seat_ = no_seat; }
    save();
}
void SystemConfig::setTrainRates(double g, double d, double c, double z, double t, double k) {
    { std::lock_guard<std::mutex> l(mutex_);
      train_rate_g_ = g; train_rate_d_ = d; train_rate_c_ = c;
      train_rate_z_ = z; train_rate_t_ = t; train_rate_k_ = k; }
    save();
}
void SystemConfig::setRefundRates(double r24h, double r2_24h, double r2h) {
    { std::lock_guard<std::mutex> l(mutex_);
      refund_rate_24h_ = r24h; refund_rate_2_24h_ = r2_24h; refund_rate_2h_ = r2h; }
    save();
}

std::string SystemConfig::toJson() const {
    std::lock_guard<std::mutex> l(mutex_);
    json j;
    j["base_rate_per_km"]   = base_rate_per_km_;
    j["seat_rate_business"]  = seat_rate_business_;
    j["seat_rate_first"]     = seat_rate_first_;
    j["seat_rate_second"]    = seat_rate_second_;
    j["seat_rate_hard_sleeper"] = seat_rate_hard_sleeper_;
    j["seat_rate_hard_seat"] = seat_rate_hard_seat_;
    j["seat_rate_no_seat"]   = seat_rate_no_seat_;
    j["train_rate_g"] = train_rate_g_;
    j["train_rate_d"] = train_rate_d_;
    j["train_rate_c"] = train_rate_c_;
    j["train_rate_z"] = train_rate_z_;
    j["train_rate_t"] = train_rate_t_;
    j["train_rate_k"] = train_rate_k_;
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
