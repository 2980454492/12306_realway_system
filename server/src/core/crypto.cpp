// crypto.cpp — AES-256-GCM 加解密实现
#include "core/crypto.h"
#include "core/logger.h"

#include <sodium.h>

#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

// 256-bit AES key
unsigned char g_key[crypto_aead_aes256gcm_KEYBYTES] = {};

bool key_loaded = false;

std::string base64Encode(const unsigned char* data, size_t len) {
    size_t b64_len = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(b64_len, '\0');
    sodium_bin2base64(&out[0], b64_len, data, len, sodium_base64_VARIANT_ORIGINAL);
    // 去除尾部 \0
    out.resize(b64_len - 1);
    return out;
}

size_t base64Decode(const std::string& b64, unsigned char* out, size_t out_max) {
    size_t decoded_len = 0;
    if (sodium_base642bin(out, out_max, b64.data(), b64.size(),
                          nullptr, &decoded_len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        return 0;
    }
    return decoded_len;
}

}  // namespace

namespace crypto {

bool initKey(const std::string& key_path) {
    if (sodium_init() < 0) {
        Logger::instance().error("libsodium init failed for crypto");
        return false;
    }

    if (fs::exists(key_path)) {
        // 加载已有密钥
        std::ifstream in(key_path, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(g_key), sizeof(g_key))) {
            Logger::instance().error("Failed to read crypto key: " + key_path);
            return false;
        }
        key_loaded = true;
        Logger::instance().info("Crypto key loaded from " + key_path);
        return true;
    }

    // 首次启动：随机生成密钥并保存
    crypto_aead_aes256gcm_keygen(g_key);

    fs::path parent = fs::path(key_path).parent_path();
    if (!parent.empty() && !fs::exists(parent))
        fs::create_directories(parent);

    std::ofstream out(key_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(g_key), sizeof(g_key));
    out.close();

    key_loaded = true;
    Logger::instance().info("Crypto key generated and saved to " + key_path);
    return true;
}

std::string encrypt(const std::string& plaintext) {
    if (!key_loaded || plaintext.empty())
        return plaintext;

    // nonce(12B) + ciphertext + mac(16B)
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    size_t clen = plaintext.size() + crypto_aead_aes256gcm_ABYTES;
    std::vector<unsigned char> ciphertext(clen);

    if (crypto_aead_aes256gcm_encrypt(
            &ciphertext[0], nullptr,
            reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
            nullptr, 0, nullptr, nonce, g_key) != 0) {
        Logger::instance().error("AES-256-GCM encryption failed");
        return plaintext;  // 失败时返回明文，不丢数据
    }

    // 拼接 nonce + ciphertext
    std::vector<unsigned char> combined(sizeof(nonce) + clen);
    std::memcpy(&combined[0], nonce, sizeof(nonce));
    std::memcpy(&combined[sizeof(nonce)], &ciphertext[0], clen);

    return base64Encode(&combined[0], combined.size());
}

std::optional<std::string> decrypt(const std::string& encrypted) {
    if (!key_loaded || encrypted.empty())
        return encrypted;

    // base64 解码
    size_t max_len = encrypted.size();
    std::vector<unsigned char> combined(max_len);
    size_t combined_len = base64Decode(encrypted, &combined[0], max_len);
    if (combined_len < crypto_aead_aes256gcm_NPUBBYTES + crypto_aead_aes256gcm_ABYTES) {
        // 可能是旧的明文数据，直接返回
        return encrypted;
    }

    const unsigned char* nonce = &combined[0];
    const unsigned char* ciphertext = &combined[crypto_aead_aes256gcm_NPUBBYTES];
    size_t ciphertext_len = combined_len - crypto_aead_aes256gcm_NPUBBYTES;

    size_t mlen = ciphertext_len;
    std::vector<unsigned char> plaintext(mlen);

    if (crypto_aead_aes256gcm_decrypt(
            &plaintext[0], nullptr, nullptr,
            ciphertext, ciphertext_len,
            nullptr, 0, nonce, g_key) != 0) {
        // 解密失败（可能是旧明文数据），返回原始值
        return encrypted;
    }

    return std::string(reinterpret_cast<char*>(&plaintext[0]), mlen - crypto_aead_aes256gcm_ABYTES);
}

}  // namespace crypto
