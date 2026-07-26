// system_config.h — 系统配置服务，运行时可变参数（票价、退票费率、列车速度等级费率）
#pragma once

#include <string>
#include <mutex>

/**
 * SystemConfig 单例 — 管理运行时可变系统参数。
 * 启动时从 config/system.json 加载，通过 API 修改后即时生效并持久化。
 * 线程安全：读写均加锁。
 *
 * 定价公式：总票价 = 里程 × 基础费率 × 列车倍率 × 席位倍率 × 张数
 */
class SystemConfig {
public:
    static SystemConfig& instance();

    SystemConfig(const SystemConfig&) = delete;
    SystemConfig& operator=(const SystemConfig&) = delete;

    /** 从 JSON 文件加载配置，失败则用默认值 */
    bool initialize(const std::string& path);

    // ── 读取 ──

    double baseRatePerKm() const;     // 二等座基准费率（元/km）
    double seatRateBusiness() const;  // 商务座
    double seatRateFirst() const;     // 一等座
    double seatRateSecond() const;    // 二等座（=1.0）
    double seatRateHardSleeper() const; // 硬卧
    double seatRateHardSeat() const;  // 硬座
    double seatRateNoSeat() const;    // 无座
    double trainRateG() const;        // 高铁 G 字头
    double trainRateD() const;        // 动车 D 字头
    double trainRateC() const;        // 城际 C 字头
    double trainRateZ() const;        // 直达 Z 字头
    double trainRateT() const;        // 特快 T 字头
    double trainRateK() const;        // 快速 K 字头
    double refundRate24h() const;     // 发车前 >24h
    double refundRate2_24h() const;   // 发车前 2-24h
    double refundRate2h() const;      // 发车前 <2h

    // ── 写入（修改后即时生效 + 持久化）──

    void setBaseRatePerKm(double rate);
    void setSeatRates(double business, double first, double second,
                      double hard_sleeper, double hard_seat, double no_seat);
    void setTrainRates(double g, double d, double c, double z, double t, double k);
    void setRefundRates(double r24h, double r2_24h, double r2h);

    /** 导出当前全部配置为 JSON 字符串 */
    std::string toJson() const;

private:
    SystemConfig() = default;

    void save() const;

    mutable std::mutex mutex_;
    std::string file_path_;

    double base_rate_per_km_ = 0.30;
    double seat_rate_business_ = 3.0;
    double seat_rate_first_    = 2.0;
    double seat_rate_second_   = 1.0;
    double seat_rate_hard_sleeper_ = 0.8;
    double seat_rate_hard_seat_    = 0.4;
    double seat_rate_no_seat_      = 0.3;
    double train_rate_g_ = 1.50;  // 高铁（300km/h 级）
    double train_rate_d_ = 0.95;  // 动车（200km/h 级）
    double train_rate_c_ = 1.10;  // 城际
    double train_rate_z_ = 0.65;  // 直达特快
    double train_rate_t_ = 0.50;  // 特快
    double train_rate_k_ = 0.40;  // 快速
    double refund_rate_24h_   = 0.95;
    double refund_rate_2_24h_ = 0.90;
    double refund_rate_2h_    = 0.80;
};
