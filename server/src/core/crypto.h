// crypto.h — AES-256-GCM 加解密工具，用于敏感字段（身份证号）静态加密
#pragma once

#include <string>
#include <optional>

/**
 * Crypto 工具 — AES-256-GCM 加密，使用 libsodium。
 * 密钥在首次启动时生成并持久化到 config/key.bin。
 * 加密数据格式：base64(nonce(12B) + ciphertext + tag(16B))
 */
namespace crypto {

/** 加载或生成密钥文件（首次启动生成） */
bool initKey(const std::string& key_path);

/** AES-256-GCM 加密，返回 base64 字符串 */
std::string encrypt(const std::string& plaintext);

/** AES-256-GCM 解密，返回明文。失败（密钥不匹配/数据损坏）返回 nullopt */
std::optional<std::string> decrypt(const std::string& encrypted);

}  // namespace crypto
