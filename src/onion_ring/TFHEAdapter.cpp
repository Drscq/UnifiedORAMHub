#include "oram/onion_ring/TFHEAdapter.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace oram::onion_ring {

namespace {

TLweParams* CreateTLweParams(const RuntimeConfig& config) {
    return new_TLweParams(config.tlwe_n, config.tlwe_k, config.alpha, 1.0 / 16.0);
}

TGswParams* CreateTGswParams(const RuntimeConfig& config, const TLweParams* tlwe_params) {
    return new_TGswParams(config.tgsw_l, config.tgsw_bgbit, tlwe_params);
}

void CheckClientTlweKey(const TFHEContext& ctx) {
    if (ctx.tlwe_key == nullptr) {
        throw std::runtime_error("TFHE client TLWE key is not initialized");
    }
}

void CheckClientTgswKey(const TFHEContext& ctx) {
    if (ctx.tgsw_key == nullptr) {
        throw std::runtime_error("TFHE client TGSW key is not initialized");
    }
}

TorusPolynomial* EncodeBlock(const std::vector<uint8_t>& block, const TLweParams* params) {
    if (block.size() > static_cast<size_t>(params->N)) {
        throw std::invalid_argument("Block size exceeds TLWE polynomial dimension");
    }

    TorusPolynomial* poly = new_TorusPolynomial(params->N);
    for (size_t i = 0; i < block.size(); ++i) {
        poly->coefsT[i] = modSwitchToTorus32(block[i], 256);
    }
    for (int i = static_cast<int>(block.size()); i < params->N; ++i) {
        poly->coefsT[i] = 0;
    }
    return poly;
}

std::vector<uint8_t> DecodeBlock(const TorusPolynomial* poly, size_t block_size) {
    std::vector<uint8_t> block(block_size, 0);
    for (size_t i = 0; i < block_size; ++i) {
        block[i] = static_cast<uint8_t>(modSwitchFromTorus32(poly->coefsT[i], 256));
    }
    return block;
}

}  // namespace

RLWECiphertext::RLWECiphertext(const TLweParams* params) : sample_(new_TLweSample(params)), params_(params) {}

RLWECiphertext::~RLWECiphertext() { Reset(); }

RLWECiphertext::RLWECiphertext(RLWECiphertext&& other) noexcept
    : sample_(std::exchange(other.sample_, nullptr)),
      params_(std::exchange(other.params_, nullptr)) {}

RLWECiphertext& RLWECiphertext::operator=(RLWECiphertext&& other) noexcept {
    if (this != &other) {
        Reset();
        sample_ = std::exchange(other.sample_, nullptr);
        params_ = std::exchange(other.params_, nullptr);
    }
    return *this;
}

std::vector<uint8_t> RLWECiphertext::Serialize() const {
    std::ostringstream stream(std::ios::binary | std::ios::out);
    export_tlweSample_toStream(stream, sample_, params_);
    const std::string serialized = stream.str();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

RLWECiphertext RLWECiphertext::Deserialize(const std::vector<uint8_t>& data,
                                           const TLweParams* params) {
    RLWECiphertext ciphertext(params);
    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);
    import_tlweSample_fromStream(stream, ciphertext.Get(), params);
    return ciphertext;
}

void RLWECiphertext::Reset() {
    if (sample_ != nullptr) {
        delete_TLweSample(sample_);
        sample_ = nullptr;
    }
    params_ = nullptr;
}

RGSWCiphertext::RGSWCiphertext(const TGswParams* params) : sample_(new_TGswSample(params)), params_(params) {}

RGSWCiphertext::~RGSWCiphertext() { Reset(); }

RGSWCiphertext::RGSWCiphertext(RGSWCiphertext&& other) noexcept
    : sample_(std::exchange(other.sample_, nullptr)),
      params_(std::exchange(other.params_, nullptr)) {}

RGSWCiphertext& RGSWCiphertext::operator=(RGSWCiphertext&& other) noexcept {
    if (this != &other) {
        Reset();
        sample_ = std::exchange(other.sample_, nullptr);
        params_ = std::exchange(other.params_, nullptr);
    }
    return *this;
}

std::vector<uint8_t> RGSWCiphertext::Serialize() const {
    std::ostringstream stream(std::ios::binary | std::ios::out);
    export_tgswSample_toStream(stream, sample_, params_);
    const std::string serialized = stream.str();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

RGSWCiphertext RGSWCiphertext::Deserialize(const std::vector<uint8_t>& data,
                                           const TGswParams* params) {
    RGSWCiphertext ciphertext(params);
    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);
    import_tgswSample_fromStream(stream, ciphertext.Get(), params);
    return ciphertext;
}

void RGSWCiphertext::Reset() {
    if (sample_ != nullptr) {
        delete_TGswSample(sample_);
        sample_ = nullptr;
    }
    params_ = nullptr;
}

TFHEContext::~TFHEContext() { Reset(); }

TFHEContext::TFHEContext(TFHEContext&& other) noexcept
    : tlwe_params(std::exchange(other.tlwe_params, nullptr)),
      tgsw_params(std::exchange(other.tgsw_params, nullptr)),
      tlwe_key(std::exchange(other.tlwe_key, nullptr)),
      tgsw_key(std::exchange(other.tgsw_key, nullptr)),
      alpha(other.alpha) {}

TFHEContext& TFHEContext::operator=(TFHEContext&& other) noexcept {
    if (this != &other) {
        Reset();
        tlwe_params = std::exchange(other.tlwe_params, nullptr);
        tgsw_params = std::exchange(other.tgsw_params, nullptr);
        tlwe_key = std::exchange(other.tlwe_key, nullptr);
        tgsw_key = std::exchange(other.tgsw_key, nullptr);
        alpha = other.alpha;
    }
    return *this;
}

TFHEContext TFHEContext::CreateClientContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.tgsw_params = CreateTGswParams(config, ctx.tlwe_params);
    ctx.tgsw_key = new_TGswKey(ctx.tgsw_params);
    tGswKeyGen(ctx.tgsw_key);
    ctx.tlwe_key = &ctx.tgsw_key->tlwe_key;
    return ctx;
}

TFHEContext TFHEContext::CreateServerContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.tgsw_params = CreateTGswParams(config, ctx.tlwe_params);
    return ctx;
}

void TFHEContext::Reset() {
    if (tgsw_key != nullptr) {
        delete_TGswKey(tgsw_key);
        tgsw_key = nullptr;
    }
    tlwe_key = nullptr;
    if (tgsw_params != nullptr) {
        delete_TGswParams(tgsw_params);
        tgsw_params = nullptr;
    }
    if (tlwe_params != nullptr) {
        delete_TLweParams(tlwe_params);
        tlwe_params = nullptr;
    }
}

RLWECiphertext EncryptBlock(const std::vector<uint8_t>& block, const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    RLWECiphertext ciphertext(ctx.tlwe_params);
    TorusPolynomial* poly = EncodeBlock(block, ctx.tlwe_params);
    tLweSymEncrypt(ciphertext.Get(), poly, ctx.alpha, ctx.tlwe_key);
    delete_TorusPolynomial(poly);
    return ciphertext;
}

std::vector<uint8_t> DecryptBlock(const RLWECiphertext& ciphertext, const TFHEContext& ctx,
                                  size_t block_size) {
    CheckClientTlweKey(ctx);
    if (block_size > static_cast<size_t>(ctx.tlwe_params->N)) {
        throw std::invalid_argument("Requested block size exceeds TLWE polynomial dimension");
    }

    TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
    tLweSymDecrypt(poly, ciphertext.Get(), ctx.tlwe_key, 256);
    std::vector<uint8_t> block = DecodeBlock(poly, block_size);
    delete_TorusPolynomial(poly);
    return block;
}

RGSWCiphertext EncryptBit(bool bit, const TFHEContext& ctx) {
    CheckClientTgswKey(ctx);
    RGSWCiphertext ciphertext(ctx.tgsw_params);
    tGswSymEncryptInt(ciphertext.Get(), bit ? 1 : 0, ctx.alpha, ctx.tgsw_key);
    return ciphertext;
}

bool DecryptBit(const RGSWCiphertext& ciphertext, const TFHEContext& ctx) {
    CheckClientTgswKey(ctx);
    const int ring_dimension = ctx.tgsw_params->tlwe_params->N;
    IntPolynomial* poly = new_IntPolynomial(ring_dimension);
    tGswSymDecrypt(poly, ciphertext.Get(), ctx.tgsw_key, 2);
    const bool bit = poly->coefs[0] != 0;
    delete_IntPolynomial(poly);
    return bit;
}

}  // namespace oram::onion_ring
