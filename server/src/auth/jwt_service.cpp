// jwt_service.cpp — JWT HS256 实现
#include "auth/jwt_service.h"
#include "core/logger.h"

#include <sodium.h>
#include <nlohmann/json.hpp>

#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

namespace {

    // ── Base64URL 编码/解码 ──
    // JWT 使用 URL-safe base64（+ → -, / → _, 去尾 =

    /** Base64 字符集 */
    const std::string BASE64_CHARS =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // Base64 URL-safe 编码（去尾 = 替换 +-/）
    std::string base64urlEncode(const std::string& input) {
        std::string encoded;
        int val = 0, valb = -6;
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(BASE64_CHARS[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            encoded.push_back(BASE64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
        }
        // URL-safe: + → -, / → _
        for (auto& ch : encoded) {
            if (ch == '+') ch = '-';
            if (ch == '/') ch = '_';
        }
        // 去掉尾部 padding =
        while (!encoded.empty() && encoded.back() == '=') {
            encoded.pop_back();
        }
        return encoded;
    }

    // Base64 URL-safe 解码
    std::string base64urlDecode(const std::string& input) {
        std::string decoded_input = input;
        // 恢复 padding
        while (decoded_input.size() % 4 != 0) {
            decoded_input += '=';
        }
        // URL-safe → standard
        for (auto& ch : decoded_input) {
            if (ch == '-') ch = '+';
            if (ch == '_') ch = '/';
        }

        std::string decoded;
        int val = 0, valb = -8;
        for (unsigned char c : decoded_input) {
            if (c == '=') break;
            auto pos = BASE64_CHARS.find(c);
            if (pos == std::string::npos) return "";
            val = (val << 6) + static_cast<int>(pos);
            valb += 6;
            if (valb >= 0) {
                decoded.push_back(static_cast<char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return decoded;
    }

    // ── HMAC-SHA256（使用 libsodium）──

    // 用 libsodium crypto_auth_hmacsha256 计算 HMAC-SHA256
    std::string hmacSha256(const std::string& key, const std::string& data) {
        unsigned char mac[crypto_auth_hmacsha256_BYTES];
        crypto_auth_hmacsha256_state state;
        crypto_auth_hmacsha256_init(&state,
            reinterpret_cast<const unsigned char*>(key.data()), key.size());
        crypto_auth_hmacsha256_update(&state,
            reinterpret_cast<const unsigned char*>(data.data()), data.size());
        crypto_auth_hmacsha256_final(&state, mac);
        return std::string(reinterpret_cast<char*>(mac), sizeof(mac));
    }

    // 生成随机密钥（256 位 = 32 字节，hex 编码）
    std::string generateRandomKey() {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (int i = 0; i < 4; ++i) {  // 4 × 64bit = 256bit
            oss << std::setw(16) << dist(rng);
        }
        return oss.str();
    }
}

// ── 单例 ──

JwtService& JwtService::instance() {
    static JwtService svc;
    return svc;
}

// ── 初始化 ──

void JwtService::initialize(const std::string& secret_key) {
    if (secret_key.empty()) {
        secret_key_ = generateRandomKey();
        Logger::instance().info("JWT secret key generated (random, 256-bit)");
    } else {
        secret_key_ = secret_key;
        Logger::instance().info("JWT secret key set from config");
    }
    initialized_ = true;
}

// ── Token 生成 ──

std::string JwtService::generateToken(const std::string& user_id,
                                      const std::string& role,
                                      int expires_in_seconds) const {
    auto now = std::chrono::system_clock::now();
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                       now.time_since_epoch()).count();

    // 1. Header
    json header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    std::string header_b64 = base64urlEncode(header.dump());

    // 2. Payload
    json payload;
    payload["sub"] = user_id;
    payload["role"] = role;
    payload["iat"] = now_sec;
    payload["exp"] = now_sec + expires_in_seconds;
    std::string payload_b64 = base64urlEncode(payload.dump());

    // 3. Signature
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string signature = hmacSha256(secret_key_, signing_input);
    std::string signature_b64 = base64urlEncode(signature);

    return signing_input + "." + signature_b64;
}

// ── Token 校验 ──

std::optional<JwtService::Payload> JwtService::verifyToken(const std::string& token) const {
    // 1. 拆分 token
    size_t dot1 = token.find('.');
    size_t dot2 = token.rfind('.');
    if (dot1 == std::string::npos || dot2 == std::string::npos || dot1 == dot2) {
        return std::nullopt;  // 格式错误
    }

    std::string header_b64 = token.substr(0, dot1);
    std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string signature_b64 = token.substr(dot2 + 1);

    // 2. 验证签名
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string expected_sig = hmacSha256(secret_key_, signing_input);
    std::string expected_sig_b64 = base64urlEncode(expected_sig);

    if (signature_b64 != expected_sig_b64) {
        return std::nullopt;  // 签名不匹配
    }

    // 3. 解析 payload
    std::string payload_json = base64urlDecode(payload_b64);
    if (payload_json.empty()) return std::nullopt;

    try {
        json payload = json::parse(payload_json);
        Payload p;
        p.user_id = payload.value("sub", "");
        p.role = payload.value("role", "");
        p.exp = payload.value("exp", 0);
        p.iat = payload.value("iat", 0);

        // 4. 检查过期
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
        if (now_sec > p.exp) {
            return std::nullopt;  // 已过期
        }

        return p;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
