// models.h — 12306 铁路票务系统核心数据模型
// 所有实体定义在一个文件中，避免循环 include。包含 JSON 序列化支持。
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <cstdint>

// ── 枚举类型 ──

/** 站点类型 */
enum class StationType : uint8_t {
    HIGH_SPEED = 0,  // 高铁站
    NORMAL     = 1,  // 普速站
    HUB        = 2   // 枢纽站（同时服务高铁和普速）
};
NLOHMANN_JSON_SERIALIZE_ENUM(StationType, {
    {StationType::HIGH_SPEED, "HIGH_SPEED"},
    {StationType::NORMAL,     "NORMAL"},
    {StationType::HUB,        "HUB"},
})

/** 线路类型 */
enum class LineType : uint8_t {
    HIGH_SPEED = 0,  // 高铁线路（设计时速 ≥250km/h）
    NORMAL     = 1,  // 普速线路（设计时速 <250km/h）
    INTERCITY  = 2   // 城际线路
};
NLOHMANN_JSON_SERIALIZE_ENUM(LineType, {
    {LineType::HIGH_SPEED, "HIGH_SPEED"},
    {LineType::NORMAL,     "NORMAL"},
    {LineType::INTERCITY,  "INTERCITY"},
})

/** 列车类型 */
enum class TrainType : uint8_t {
    REGULAR   = 0,  // 图定列车（长期运行）
    TEMPORARY = 1   // 临客（指定有效期，过期自动停运）
};
NLOHMANN_JSON_SERIALIZE_ENUM(TrainType, {
    {TrainType::REGULAR,   "REGULAR"},
    {TrainType::TEMPORARY, "TEMPORARY"},
})

/** 列车状态 */
enum class TrainStatus : uint8_t {
    ACTIVE    = 0,  // 运行中
    SUSPENDED = 1,  // 已停运
    ARCHIVED  = 2   // 已归档
};
NLOHMANN_JSON_SERIALIZE_ENUM(TrainStatus, {
    {TrainStatus::ACTIVE,    "ACTIVE"},
    {TrainStatus::SUSPENDED, "SUSPENDED"},
    {TrainStatus::ARCHIVED,  "ARCHIVED"},
})

/** 席位类型 */
enum class SeatType : uint8_t {
    BUSINESS     = 0,  // 商务座
    FIRST        = 1,  // 一等座
    SECOND       = 2,  // 二等座
    HARD_SLEEPER = 3,  // 硬卧
    HARD_SEAT    = 4,  // 硬座
    NO_SEAT      = 5   // 无座
};
NLOHMANN_JSON_SERIALIZE_ENUM(SeatType, {
    {SeatType::BUSINESS,     "BUSINESS"},
    {SeatType::FIRST,        "FIRST"},
    {SeatType::SECOND,       "SECOND"},
    {SeatType::HARD_SLEEPER, "HARD_SLEEPER"},
    {SeatType::HARD_SEAT,    "HARD_SEAT"},
    {SeatType::NO_SEAT,      "NO_SEAT"},
})

/** 订单状态 */
enum class OrderStatus : uint8_t {
    PAID    = 0,  // 已支付
    REFUNDED = 1   // 已退票
};
NLOHMANN_JSON_SERIALIZE_ENUM(OrderStatus, {
    {OrderStatus::PAID,    "PAID"},
    {OrderStatus::REFUNDED, "REFUNDED"},
})

/** 用户角色 — RBAC 三角色 */
enum class UserRole : uint8_t {
    PASSENGER = 0,  // 普通旅客
    STAFF     = 1,  // 铁路职工
    ADMIN     = 2   // 系统管理员
};
NLOHMANN_JSON_SERIALIZE_ENUM(UserRole, {
    {UserRole::PASSENGER, "PASSENGER"},
    {UserRole::STAFF,     "STAFF"},
    {UserRole::ADMIN,     "ADMIN"},
})

// ── 核心数据结构 ──

/** 经停站 — 列车在一个站点的到发信息。时间用 HHMM 整数（如 1430=14:30）。
 *  arrival/departure 默认 0 表示未初始化；-1 表示无到达（始发站）或无发车（终到站）。
 *  实际运行中不存在 0000 时刻的列车，故 0 不会产生歧义。 */
struct Stop {
    uint32_t station_id = 0;
    int arrival = 0;        // 到站时间 HHMM，-1 表示始发站（无到达）
    int departure = 0;      // 发车时间 HHMM，-1 表示终到站（无出发）
    uint8_t platform = 0;   // 站台号，0 表示未指定
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Stop, station_id, arrival, departure, platform)

/** 席位配置 — 一列车各席位的座位数量 */
struct SeatConfig {
    uint16_t business_seats = 0;
    uint16_t first_seats = 0;
    uint16_t second_seats = 0;
    uint16_t hard_sleeper = 0;
    uint16_t hard_seat = 0;
    uint16_t no_seat = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SeatConfig,
    business_seats, first_seats, second_seats,
    hard_sleeper, hard_seat, no_seat)

/** 站点 — 铁路网中的节点 */
struct Station {
    uint32_t id = 0;
    std::string name;
    std::string city;
    StationType type = StationType::NORMAL;
    double latitude = 0.0;   // 纬度
    double longitude = 0.0;  // 经度
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Station, id, name, city, type, latitude, longitude)

/** 线路 — 铁路网中的边，含沿途所有站点序列（含端点和中间站） */
struct Line {
    uint32_t id = 0;
    std::string name;                   // 线路名（如"京包高铁"）
    std::vector<uint32_t> stations;     // 沿途站点 ID 序列（按地理方向，首个=起点，末个=终点）
    double distance_km = 0.0;           // 里程（公里）
    uint32_t max_speed_kmh = 0;         // 设计时速
    LineType type = LineType::NORMAL;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Line,
    id, name, stations, distance_km, max_speed_kmh, type)

/** 列车 — 一列在铁路上运行的客运列车 */
struct Train {
    std::string id;               // 车次号（如 G2492 / K7901 / C1003 / L6601）
    TrainType type = TrainType::REGULAR;
    std::vector<Stop> stops;      // 停站序列（有到发时间，仅办客站）
    std::vector<uint32_t> route_stations;  // 经过站序列（含停站+经过不停车，用于计算票价里程）
    TrainStatus status = TrainStatus::ACTIVE;
    SeatConfig seat_config;       // 各席位座位数
    std::string valid_from;       // 有效期起始（临客必填，图定可为空）
    std::string valid_until;      // 有效期截止
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Train,
    id, type, stops, route_stations, status, seat_config, valid_from, valid_until)

/** 订单 — 旅客购票记录。Phase 5 实现完整业务逻辑 */
struct Order {
    std::string id;               // UUID
    std::string user_id;
    std::string train_id;
    std::string date;             // 乘车日期 yyyy-MM-dd
    uint32_t from_station = 0;
    uint32_t to_station = 0;
    SeatType seat_type = SeatType::SECOND;
    uint16_t seat_number = 0;     // 分配的具体座位号
    double price = 0.0;           // 票价（元）
    OrderStatus status = OrderStatus::PAID;
    std::string created_at;       // ISO 8601 时间戳
    std::string passenger_name;   // 乘车人姓名
    std::string passenger_id;     // 身份证号（存盘时已加密）
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Order,
    id, user_id, train_id, date, from_station, to_station,
    seat_type, seat_number, price, status, created_at,
    passenger_name, passenger_id)

/** 用户 — 系统认证与 RBAC 的基础实体 */
struct User {
    std::string id;               // UUID
    std::string username;
    std::string password_hash;    // argon2id 输出字符串（crypto_pwhash_str，含算法+参数+salt+hash）
    UserRole role = UserRole::PASSENGER;
    bool active = true;           // false = 已禁用
    int failed_attempts = 0;      // 连续登录失败次数
    std::string locked_until;     // 锁定截止时间 ISO 8601，空表示未锁定
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(User,
    id, username, password_hash, role, active, failed_attempts, locked_until)
