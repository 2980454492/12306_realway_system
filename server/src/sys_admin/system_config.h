// system_config.h — 系统配置服务，票价矩阵 + 退票费率
#pragma once

#include "models.h"

#include <string>
#include <mutex>

/**
 * SystemConfig 单例 — 管理运行时可变系统参数。
 * 票价 = 里程 × 费率(列车类型, 席位) × 张数。费率单位：元/km。
 * 启动时从 config/system.json 加载，API 修改后即时生效。
 */
class SystemConfig {
public:
    static SystemConfig& instance();

    SystemConfig(const SystemConfig&) = delete;
    SystemConfig& operator=(const SystemConfig&) = delete;

    bool initialize(const std::string& path);

    // ── 票价查询 ──

    /** 根据车次号前缀（G/D/C/Z/T/K）和席位查询费率（元/km） */
    double ratePerKm(const std::string& train_id, SeatType seat) const;

    // ── 退票费率 ──

    double refundRate24h() const;
    double refundRate2_24h() const;
    double refundRate2h() const;

    // ── 写入 ──

    /** 设置某列车类型某席位的费率（元/km） */
    void setRate(char prefix, SeatType seat, double rate);

    void setRefundRates(double r24h, double r2_24h, double r2h);

    /** 导出当前配置为 JSON */
    std::string toJson() const;

private:
    SystemConfig() = default;
    void save() const;

    // 用 seatTypeToKey/prefixToKey 压缩到连续数组
    static int seatIdx(SeatType s);
    static int prefixIdx(char p);
    static char prefixChar(int idx);

    mutable std::mutex mutex_;
    std::string file_path_;

    // 费率矩阵：prefixIdx(0..6) × seatIdx(0..5) → 元/km
    // 7 个列车类型（G,D,C,Z,T,K,其他）× 6 个席位
    double rates_[7][6] = {};

    double refund_rate_24h_   = 0.95;
    double refund_rate_2_24h_ = 0.90;
    double refund_rate_2h_    = 0.80;
};
