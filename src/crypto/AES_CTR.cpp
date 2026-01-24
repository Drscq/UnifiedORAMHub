#include "oram/crypto/AES_CTR.h"

#include <openssl/err.h>
#include <openssl/rand.h>

#include <cstring>
#include <stdexcept>

namespace oram::crypto {

AES_CTR::AES_CTR(const std::vector<uint8_t>& key) : AES_CTR(key.data(), key.size()) {}

AES_CTR::AES_CTR(const uint8_t* key, size_t key_len) {
    if (key_len == kKeySize128) {
        cipher_ = EVP_aes_128_ctr();
    } else if (key_len == kKeySize256) {
        cipher_ = EVP_aes_256_ctr();
    } else {
        throw std::invalid_argument("Key size must be 16 (AES-128) or 32 (AES-256) bytes");
    }

    key_.assign(key, key + key_len);
}

AES_CTR::~AES_CTR() {
    // Securely wipe the key from memory
    if (!key_.empty()) {
        OPENSSL_cleanse(key_.data(), key_.size());
    }
}

AES_CTR::AES_CTR(AES_CTR&& other) noexcept
    : key_(std::move(other.key_)), cipher_(other.cipher_), ctx_(std::move(other.ctx_)) {
    other.cipher_ = nullptr;
}

AES_CTR& AES_CTR::operator=(AES_CTR&& other) noexcept {
    if (this != &other) {
        // Securely wipe current key
        if (!key_.empty()) {
            OPENSSL_cleanse(key_.data(), key_.size());
        }

        key_ = std::move(other.key_);
        cipher_ = other.cipher_;
        ctx_ = std::move(other.ctx_);
        other.cipher_ = nullptr;
    }
    return *this;
}

void AES_CTR::CipherOperation(uint8_t* output, const uint8_t* input, size_t len,
                              const uint8_t* iv) {
    // Reset context for reuse
    ctx_.reset();

    if (EVP_EncryptInit_ex(ctx_.get(), cipher_, nullptr, key_.data(), iv) != 1) {
        throw std::runtime_error("Failed to initialize cipher");
    }

    int out_len = 0;
    if (EVP_EncryptUpdate(ctx_.get(), output, &out_len, input, static_cast<int>(len)) != 1) {
        throw std::runtime_error("Failed in cipher operation");
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx_.get(), output + out_len, &final_len) != 1) {
        throw std::runtime_error("Failed to finalize cipher");
    }
}

std::vector<uint8_t> AES_CTR::Encrypt(const std::vector<uint8_t>& plaintext,
                                      const std::vector<uint8_t>& iv) {
    if (iv.size() != kIVSize) {
        throw std::invalid_argument("IV must be 16 bytes");
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    CipherOperation(ciphertext.data(), plaintext.data(), plaintext.size(), iv.data());
    return ciphertext;
}

std::vector<uint8_t> AES_CTR::Decrypt(const std::vector<uint8_t>& ciphertext,
                                      const std::vector<uint8_t>& iv) {
    // CTR mode: decrypt == encrypt
    return Encrypt(ciphertext, iv);
}

void AES_CTR::EncryptInPlace(uint8_t* data, size_t len, const uint8_t* iv) {
    CipherOperation(data, data, len, iv);
}

void AES_CTR::DecryptInPlace(uint8_t* data, size_t len, const uint8_t* iv) {
    // CTR mode: decrypt == encrypt
    EncryptInPlace(data, len, iv);
}

void AES_CTR::EncryptBlocks(const std::vector<std::vector<uint8_t>>& plaintexts,
                            const std::vector<std::vector<uint8_t>>& ivs,
                            std::vector<std::vector<uint8_t>>& ciphertexts) {
    if (plaintexts.size() != ivs.size()) {
        throw std::invalid_argument("Number of plaintexts must match number of IVs");
    }

    ciphertexts.resize(plaintexts.size());

    for (size_t i = 0; i < plaintexts.size(); ++i) {
        if (ivs[i].size() != kIVSize) {
            throw std::invalid_argument("Each IV must be 16 bytes");
        }
        ciphertexts[i].resize(plaintexts[i].size());
        CipherOperation(ciphertexts[i].data(), plaintexts[i].data(), plaintexts[i].size(),
                        ivs[i].data());
    }
}

std::vector<uint8_t> AES_CTR::GenerateKey(size_t key_size) {
    if (key_size != kKeySize128 && key_size != kKeySize256) {
        throw std::invalid_argument("Key size must be 16 or 32 bytes");
    }

    std::vector<uint8_t> key(key_size);
    if (RAND_bytes(key.data(), static_cast<int>(key_size)) != 1) {
        throw std::runtime_error("Failed to generate random key");
    }
    return key;
}

std::vector<uint8_t> AES_CTR::GenerateIV() {
    std::vector<uint8_t> iv(kIVSize);
    if (RAND_bytes(iv.data(), static_cast<int>(kIVSize)) != 1) {
        throw std::runtime_error("Failed to generate random IV");
    }
    return iv;
}

void AES_CTR::IncrementCounter(uint8_t* iv, size_t increment) {
    // Treat IV as big-endian 128-bit integer and increment
    // Start from the last byte (least significant)
    for (int i = kIVSize - 1; i >= 0 && increment > 0; --i) {
        size_t sum = static_cast<size_t>(iv[i]) + (increment & 0xFF);
        iv[i] = static_cast<uint8_t>(sum & 0xFF);
        increment = (increment >> 8) + (sum >> 8);
    }
}

}  // namespace oram::crypto
