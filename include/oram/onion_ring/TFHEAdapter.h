#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "oram/onion_ring/Config.h"

#include "tfhe.h"

namespace oram::onion_ring {

class RLWECiphertext {
   public:
    explicit RLWECiphertext(const TLweParams* params);
    ~RLWECiphertext();

    RLWECiphertext(const RLWECiphertext&) = delete;
    RLWECiphertext& operator=(const RLWECiphertext&) = delete;

    RLWECiphertext(RLWECiphertext&& other) noexcept;
    RLWECiphertext& operator=(RLWECiphertext&& other) noexcept;

    TLweSample* Get() { return sample_; }
    const TLweSample* Get() const { return sample_; }
    const TLweParams* Params() const { return params_; }

    std::vector<uint8_t> Serialize() const;
    static RLWECiphertext Deserialize(const std::vector<uint8_t>& data,
                                      const TLweParams* params);

   private:
    void Reset();

    TLweSample* sample_ = nullptr;
    const TLweParams* params_ = nullptr;
};

class RGSWCiphertext {
   public:
    explicit RGSWCiphertext(const TGswParams* params);
    ~RGSWCiphertext();

    RGSWCiphertext(const RGSWCiphertext&) = delete;
    RGSWCiphertext& operator=(const RGSWCiphertext&) = delete;

    RGSWCiphertext(RGSWCiphertext&& other) noexcept;
    RGSWCiphertext& operator=(RGSWCiphertext&& other) noexcept;

    TGswSample* Get() { return sample_; }
    const TGswSample* Get() const { return sample_; }
    const TGswParams* Params() const { return params_; }

    std::vector<uint8_t> Serialize() const;
    static RGSWCiphertext Deserialize(const std::vector<uint8_t>& data,
                                      const TGswParams* params);

   private:
    void Reset();

    TGswSample* sample_ = nullptr;
    const TGswParams* params_ = nullptr;
};

struct TFHEContext {
    TLweParams* tlwe_params = nullptr;
    TGswParams* tgsw_params = nullptr;
    TLweKey* tlwe_key = nullptr;
    TGswKey* tgsw_key = nullptr;
    double alpha = 0.0;

    TFHEContext() = default;
    ~TFHEContext();

    TFHEContext(const TFHEContext&) = delete;
    TFHEContext& operator=(const TFHEContext&) = delete;

    TFHEContext(TFHEContext&& other) noexcept;
    TFHEContext& operator=(TFHEContext&& other) noexcept;

    static TFHEContext CreateClientContext(const RuntimeConfig& config);
    static TFHEContext CreateServerContext(const RuntimeConfig& config);

    void Reset();
};

RLWECiphertext EncryptBlock(const std::vector<uint8_t>& block, const TFHEContext& ctx);
std::vector<uint8_t> DecryptBlock(const RLWECiphertext& ciphertext, const TFHEContext& ctx,
                                  size_t block_size);
RGSWCiphertext EncryptBit(bool bit, const TFHEContext& ctx);
bool DecryptBit(const RGSWCiphertext& ciphertext, const TFHEContext& ctx);

}  // namespace oram::onion_ring
