#include "oram/onion_ring/TFHEAdapter.h"

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "lwe-functions.h"
#include "tfhe_io.h"
#include "tlwe_functions.h"

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

void WriteUint64(std::ostream* stream, uint64_t value) {
    stream->write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint64_t ReadUint64(std::istream* stream) {
    uint64_t value = 0;
    stream->read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!*stream) {
        throw std::runtime_error("Failed to read uint64 from serialized data");
    }
    return value;
}

void WriteBytes(std::ostream* stream, const std::vector<uint8_t>& bytes) {
    WriteUint64(stream, bytes.size());
    if (!bytes.empty()) {
        stream->write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
    }
}

std::vector<uint8_t> ReadBytes(std::istream* stream) {
    const uint64_t size = ReadUint64(stream);
    std::vector<uint8_t> bytes(size, 0);
    if (size > 0) {
        stream->read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!*stream) {
        throw std::runtime_error("Failed to read byte vector from serialized data");
    }
    return bytes;
}

LweKeySwitchKeyHandle BuildIdentityKeySwitchKey(const TFHEContext& ctx) {
    const LweParams* extract_params = &ctx.tlwe_params->extracted_lweparams;
    LweKey* extracted_key = new_LweKey(extract_params);
    tLweExtractKey(extracted_key, ctx.tlwe_key);

    LweKeySwitchKey* key_switch = new_LweKeySwitchKey(extract_params->n, 8, 2, extract_params);
    lweCreateKeySwitchKey(key_switch, extracted_key, extracted_key);
    delete_LweKey(extracted_key);
    return LweKeySwitchKeyHandle(key_switch);
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

LweKeySwitchKeyHandle::LweKeySwitchKeyHandle(LweKeySwitchKey* key) : key_(key) {}

LweKeySwitchKeyHandle::~LweKeySwitchKeyHandle() { Reset(); }

LweKeySwitchKeyHandle::LweKeySwitchKeyHandle(LweKeySwitchKeyHandle&& other) noexcept
    : key_(std::exchange(other.key_, nullptr)) {}

LweKeySwitchKeyHandle& LweKeySwitchKeyHandle::operator=(LweKeySwitchKeyHandle&& other) noexcept {
    if (this != &other) {
        Reset();
        key_ = std::exchange(other.key_, nullptr);
    }
    return *this;
}

std::vector<uint8_t> LweKeySwitchKeyHandle::Serialize() const {
    if (key_ == nullptr) {
        return {};
    }

    std::ostringstream stream(std::ios::binary | std::ios::out);
    export_lweKeySwitchKey_toStream(stream, key_);
    const std::string serialized = stream.str();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

LweKeySwitchKeyHandle LweKeySwitchKeyHandle::Deserialize(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return LweKeySwitchKeyHandle();
    }

    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);
    return LweKeySwitchKeyHandle(new_lweKeySwitchKey_fromStream(stream));
}

void LweKeySwitchKeyHandle::Reset() {
    if (key_ != nullptr) {
        delete_LweKeySwitchKey(key_);
        key_ = nullptr;
    }
}

std::vector<uint8_t> ExpansionBundle::Serialize() const {
    std::ostringstream stream(std::ios::binary | std::ios::out);

    WriteUint64(&stream, substitution_keys.size());
    for (const auto& key : substitution_keys) {
        WriteBytes(&stream, key.Serialize());
    }

    WriteUint64(&stream, lwe_key_switch_keys.size());
    for (const auto& key : lwe_key_switch_keys) {
        WriteBytes(&stream, key.Serialize());
    }

    WriteBytes(&stream, neg_sk_rgsw_bytes);

    const std::string serialized = stream.str();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

ExpansionBundle ExpansionBundle::Deserialize(const std::vector<uint8_t>& data,
                                            const RuntimeConfig& config,
                                            const TLweParams* tlwe_params,
                                            const TGswParams* tgsw_params) {
    (void)config;
    (void)tlwe_params;

    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);

    ExpansionBundle bundle;

    const uint64_t substitution_key_count = ReadUint64(&stream);
    bundle.substitution_keys.reserve(substitution_key_count);
    for (uint64_t i = 0; i < substitution_key_count; ++i) {
        bundle.substitution_keys.emplace_back(
            RGSWCiphertext::Deserialize(ReadBytes(&stream), tgsw_params));
    }

    const uint64_t key_switch_count = ReadUint64(&stream);
    bundle.lwe_key_switch_keys.reserve(key_switch_count);
    for (uint64_t i = 0; i < key_switch_count; ++i) {
        bundle.lwe_key_switch_keys.emplace_back(
            LweKeySwitchKeyHandle::Deserialize(ReadBytes(&stream)));
    }

    bundle.neg_sk_rgsw_bytes = ReadBytes(&stream);
    return bundle;
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

ExpansionBundle BuildExpansionBundle(const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    CheckClientTgswKey(ctx);

    ExpansionBundle bundle;

    size_t level_count = 0;
    for (int n = ctx.tlwe_params->N; n > 1; n >>= 1) {
        ++level_count;
    }

    bundle.substitution_keys.reserve(level_count);
    bundle.lwe_key_switch_keys.reserve(level_count);
    for (size_t level = 0; level < level_count; ++level) {
        bundle.substitution_keys.emplace_back(EncryptBit(false, ctx));
        bundle.lwe_key_switch_keys.emplace_back(BuildIdentityKeySwitchKey(ctx));
    }

    bundle.neg_sk_rgsw_bytes = EncryptBit(false, ctx).Serialize();
    return bundle;
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
