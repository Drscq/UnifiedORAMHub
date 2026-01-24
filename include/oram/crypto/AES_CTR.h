#pragma once

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace oram::crypto {

// RAII wrapper for EVP_CIPHER_CTX
class CipherContext {
   public:
    CipherContext() : ctx_(EVP_CIPHER_CTX_new()) {
        if (!ctx_) {
            throw std::runtime_error("Failed to create cipher context");
        }
    }

    ~CipherContext() {
        if (ctx_) {
            EVP_CIPHER_CTX_free(ctx_);
        }
    }

    // Non-copyable
    CipherContext(const CipherContext&) = delete;
    CipherContext& operator=(const CipherContext&) = delete;

    // Movable
    CipherContext(CipherContext&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }

    CipherContext& operator=(CipherContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) EVP_CIPHER_CTX_free(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    EVP_CIPHER_CTX* get() { return ctx_; }

    void reset() { EVP_CIPHER_CTX_reset(ctx_); }

   private:
    EVP_CIPHER_CTX* ctx_;
};

// High-performance AES-128/256 CTR mode encryption using OpenSSL
// Optimized for ORAM workloads with reusable context
class AES_CTR {
   public:
    static constexpr size_t kKeySize128 = 16;
    static constexpr size_t kKeySize256 = 32;
    static constexpr size_t kIVSize = 16;  // AES block size
    static constexpr size_t kBlockSize = 16;

    // Constructor with key (16 bytes for AES-128, 32 bytes for AES-256)
    explicit AES_CTR(const std::vector<uint8_t>& key);
    explicit AES_CTR(const uint8_t* key, size_t key_len);
    ~AES_CTR();

    // Non-copyable (key security)
    AES_CTR(const AES_CTR&) = delete;
    AES_CTR& operator=(const AES_CTR&) = delete;

    // Movable
    AES_CTR(AES_CTR&& other) noexcept;
    AES_CTR& operator=(AES_CTR&& other) noexcept;

    // Encrypt/Decrypt (same operation in CTR mode)
    // Returns new buffer
    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext,
                                 const std::vector<uint8_t>& iv);

    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& ciphertext,
                                 const std::vector<uint8_t>& iv);

    // In-place encryption/decryption (more efficient for fixed-size blocks)
    void EncryptInPlace(uint8_t* data, size_t len, const uint8_t* iv);
    void DecryptInPlace(uint8_t* data, size_t len, const uint8_t* iv);

    // Batch operation for multiple blocks with same key but different IVs
    void EncryptBlocks(const std::vector<std::vector<uint8_t>>& plaintexts,
                       const std::vector<std::vector<uint8_t>>& ivs,
                       std::vector<std::vector<uint8_t>>& ciphertexts);

    // Generate a random key
    static std::vector<uint8_t> GenerateKey(size_t key_size = kKeySize128);

    // Generate a random IV
    static std::vector<uint8_t> GenerateIV();

    // Increment counter (useful for sequential block encryption)
    static void IncrementCounter(uint8_t* iv, size_t increment = 1);

   private:
    void CipherOperation(uint8_t* output, const uint8_t* input, size_t len, const uint8_t* iv);

    std::vector<uint8_t> key_;
    const EVP_CIPHER* cipher_;
    CipherContext ctx_;  // Reusable context
};

}  // namespace oram::crypto
