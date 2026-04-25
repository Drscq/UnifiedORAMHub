#include "oram/onion_ring/TFHEAdapter.h"

#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "lwe-functions.h"
#include "numeric_functions.h"
#include "polynomials.h"
#include "tfhe_io.h"
#include "tlwe_functions.h"

namespace oram::onion_ring {

namespace {

int PlaintextModulus(int32_t plaintext_bits) {
    if (plaintext_bits <= 0 || plaintext_bits > 30) {
        throw std::invalid_argument("Plaintext precision must be in the range [1, 30] bits");
    }
    return 1 << plaintext_bits;
}

TLweParams* CreateTLweParams(const RuntimeConfig& config) {
    const int32_t n = config.use_recursive_packed_swap_bits ? config.recursive_tlwe_n : config.tlwe_n;
    return new_TLweParams(n, config.tlwe_k, config.alpha,
                          1.0 / static_cast<double>(PlaintextModulus(config.plaintext_bits)));
}

TGswParams* CreateTGswParams(int32_t l, int32_t bgbit, const TLweParams* tlwe_params) {
    return new_TGswParams(l, bgbit, tlwe_params);
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

void CheckClientSwapTgswKey(const TFHEContext& ctx) {
    if (ctx.swap_tgsw_key == nullptr) {
        throw std::runtime_error("TFHE client swap TGSW key is not initialized");
    }
}

void CheckClientPracticalTgswKey(const TFHEContext& ctx) {
    if (ctx.practical_tgsw_key == nullptr) {
        throw std::runtime_error("TFHE client practical TGSW key is not initialized");
    }
}

void CheckClientNegSkTgswKey(const TFHEContext& ctx) {
    if (ctx.neg_sk_tgsw_key == nullptr) {
        throw std::runtime_error("TFHE client negated-secret-key TGSW key is not initialized");
    }
}

void CopyTlweSecretKey(TLweKey* dst, const TLweKey* src) {
    if (dst == nullptr || src == nullptr || dst->params->N != src->params->N ||
        dst->params->k != src->params->k) {
        throw std::invalid_argument("Cannot copy incompatible TLWE secret keys");
    }
    for (int block = 0; block < src->params->k; ++block) {
        for (int coeff = 0; coeff < src->params->N; ++coeff) {
            dst->key[block].coefs[coeff] = src->key[block].coefs[coeff];
        }
    }
}

void PopulateExpansionMetadata(ExpansionBundle* bundle, const TFHEContext& ctx) {
    bundle->swap_l = ctx.swap_tgsw_params != nullptr ? ctx.swap_tgsw_params->l : 0;
    bundle->swap_bgbit = ctx.swap_tgsw_params != nullptr ? ctx.swap_tgsw_params->Bgbit : 0;
    bundle->neg_sk_l = ctx.neg_sk_tgsw_params != nullptr ? ctx.neg_sk_tgsw_params->l : 0;
    bundle->neg_sk_bgbit = ctx.neg_sk_tgsw_params != nullptr ? ctx.neg_sk_tgsw_params->Bgbit : 0;
    bundle->torus_bits = TFHE_TORUS_BITS;
}

void ValidateExpansionMetadata(const ExpansionBundle& bundle, const RuntimeConfig& config) {
    if (bundle.torus_bits != TFHE_TORUS_BITS) {
        throw std::runtime_error("Expansion bundle torus width does not match runtime backend");
    }
    if (bundle.swap_l != config.swap_tgsw_l || bundle.swap_bgbit != config.swap_tgsw_bgbit ||
        bundle.neg_sk_l != config.neg_sk_tgsw_l ||
        bundle.neg_sk_bgbit != config.neg_sk_tgsw_bgbit) {
        throw std::runtime_error("Expansion bundle gadget parameters do not match runtime config");
    }
}

TorusPolynomial* EncodeBlock(const std::vector<uint8_t>& block, const TLweParams* params,
                             int32_t plaintext_bits) {
    const size_t capacity_bytes =
        (static_cast<size_t>(params->N) * static_cast<size_t>(plaintext_bits)) / 8;
    if (block.size() > capacity_bytes) {
        throw std::invalid_argument("Block size exceeds TLWE polynomial plaintext capacity");
    }

    TorusPolynomial* poly = new_TorusPolynomial(params->N);
    const int plaintext_modulus = PlaintextModulus(plaintext_bits);
    for (int coeff = 0; coeff < params->N; ++coeff) {
        int symbol = 0;
        const size_t symbol_offset = static_cast<size_t>(coeff) * static_cast<size_t>(plaintext_bits);
        for (int32_t bit = 0; bit < plaintext_bits; ++bit) {
            const size_t bit_offset = symbol_offset + static_cast<size_t>(bit);
            const size_t byte_index = bit_offset / 8;
            if (byte_index >= block.size()) {
                break;
            }
            const int byte_bit = static_cast<int>(bit_offset % 8);
            if ((block[byte_index] & static_cast<uint8_t>(1U << byte_bit)) != 0) {
                symbol |= 1 << bit;
            }
        }
        poly->coefsT[coeff] = modSwitchToTorus32(symbol, plaintext_modulus);
    }
    return poly;
}

std::vector<uint8_t> DecodeBlock(const TorusPolynomial* poly, size_t block_size,
                                 int32_t plaintext_bits) {
    std::vector<uint8_t> block(block_size, 0);
    const int plaintext_modulus = PlaintextModulus(plaintext_bits);
    for (int coeff = 0; coeff < poly->N; ++coeff) {
        const int symbol = modSwitchFromTorus32(poly->coefsT[coeff], plaintext_modulus);
        const size_t symbol_offset = static_cast<size_t>(coeff) * static_cast<size_t>(plaintext_bits);
        for (int32_t bit = 0; bit < plaintext_bits; ++bit) {
            if ((symbol & (1 << bit)) == 0) {
                continue;
            }
            const size_t bit_offset = symbol_offset + static_cast<size_t>(bit);
            const size_t byte_index = bit_offset / 8;
            if (byte_index >= block_size) {
                break;
            }
            const int byte_bit = static_cast<int>(bit_offset % 8);
            block[byte_index] |= static_cast<uint8_t>(1U << byte_bit);
        }
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

void ApplyAutomorphismToIntPolynomial(IntPolynomial* result, const IntPolynomial* input, int32_t power) {
    const int n = input->N;
    if ((power & 1) == 0) {
        throw std::invalid_argument("Automorphism power must be odd");
    }

    for (int coeff = 0; coeff < n; ++coeff) {
        result->coefs[coeff] = 0;
    }
    for (int coeff = 0; coeff < n; ++coeff) {
        const int64_t mapped = (static_cast<int64_t>(coeff) * power) % (2LL * n);
        if (mapped < n) {
            result->coefs[mapped] = input->coefs[coeff];
        } else {
            result->coefs[mapped - n] = -input->coefs[coeff];
        }
    }
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

RGSWCiphertext BuildNegatedSecretKeyCiphertext(const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    CheckClientNegSkTgswKey(ctx);
    if (ctx.tlwe_params->k != 1) {
        throw std::invalid_argument("Negated-secret-key support currently expects TLWE k == 1");
    }

    IntPolynomial* neg_secret = new_IntPolynomial(ctx.tlwe_params->N);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        neg_secret->coefs[coeff] = -ctx.tlwe_key->key[0].coefs[coeff];
    }

    RGSWCiphertext ciphertext(ctx.neg_sk_tgsw_params);
    tGswSymEncrypt(ciphertext.Get(), neg_secret, 0.0, ctx.neg_sk_tgsw_key);
    delete_IntPolynomial(neg_secret);
    return ciphertext;
}

RGSWCiphertext BuildSubstitutionMonomialCiphertext(const TFHEContext& ctx, int exponent) {
    CheckClientSwapTgswKey(ctx);
    if (exponent <= 0 || exponent >= ctx.tlwe_params->N) {
        throw std::out_of_range("Substitution monomial exponent must be in (0, N)");
    }

    IntPolynomial* monomial = new_IntPolynomial(ctx.tlwe_params->N);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        monomial->coefs[coeff] = 0;
    }
    monomial->coefs[exponent] = 1;

    RGSWCiphertext ciphertext(ctx.swap_tgsw_params);
    tGswSymEncrypt(ciphertext.Get(), monomial, ctx.alpha, ctx.swap_tgsw_key);
    delete_IntPolynomial(monomial);
    return ciphertext;
}

RecursiveRlweKeySwitchKey BuildRecursiveRlweKeySwitchKey(const TFHEContext& ctx, int32_t power,
                                                         int32_t basebit, int32_t length) {
    if (ctx.tlwe_key == nullptr || ctx.tlwe_params == nullptr) {
        throw std::invalid_argument("Recursive RLWE key switching requires initialized TLWE key material");
    }
    if ((power & 1) == 0) {
        throw std::invalid_argument("Recursive RLWE substitution power must be odd");
    }
    if (basebit <= 0 || length <= 0 || basebit * length > 63) {
        throw std::invalid_argument("Recursive RLWE key switching requires Torus64-compatible decomposition parameters");
    }

    RecursiveRlweKeySwitchKey key;
    key.substitution_power = power;
    key.basebit = basebit;

    TGswParams* decomp_params = new_TGswParams(length, basebit, ctx.tlwe_params);
    IntPolynomial* transformed_secret = new_IntPolynomial(ctx.tlwe_params->N);
    ApplyAutomorphismToIntPolynomial(transformed_secret, &ctx.tlwe_key->key[0], power);

    key.levels.reserve(static_cast<size_t>(length));
    for (int level = 0; level < length; ++level) {
        RLWECiphertext row(ctx.tlwe_params);
        TorusPolynomial* message = new_TorusPolynomial(ctx.tlwe_params->N);
        const Torus32 scale = decomp_params->h[level];
        for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
            message->coefsT[coeff] = transformed_secret->coefs[coeff] * scale;
        }
        tLweSymEncrypt(row.Get(), message, ctx.alpha, ctx.tlwe_key);
        delete_TorusPolynomial(message);
        key.levels.emplace_back(std::move(row));
    }

    delete_IntPolynomial(transformed_secret);
    delete_TGswParams(decomp_params);
    return key;
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

std::vector<uint8_t> RecursiveRlweKeySwitchKey::Serialize() const {
    std::ostringstream stream(std::ios::binary | std::ios::out);
    WriteUint64(&stream, static_cast<uint64_t>(substitution_power));
    WriteUint64(&stream, static_cast<uint64_t>(basebit));
    WriteUint64(&stream, levels.size());
    for (const auto& level : levels) {
        WriteBytes(&stream, level.Serialize());
    }
    const std::string serialized = stream.str();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

RecursiveRlweKeySwitchKey RecursiveRlweKeySwitchKey::Deserialize(const std::vector<uint8_t>& data,
                                                                 const TLweParams* params) {
    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);

    RecursiveRlweKeySwitchKey key;
    key.substitution_power = static_cast<int32_t>(ReadUint64(&stream));
    key.basebit = static_cast<int32_t>(ReadUint64(&stream));
    const uint64_t level_count = ReadUint64(&stream);
    key.levels.reserve(level_count);
    for (uint64_t i = 0; i < level_count; ++i) {
        key.levels.emplace_back(RLWECiphertext::Deserialize(ReadBytes(&stream), params));
    }
    return key;
}

std::vector<uint8_t> ExpansionBundle::Serialize() const {
    std::ostringstream stream(std::ios::binary | std::ios::out);

    WriteUint64(&stream, static_cast<uint64_t>(swap_l));
    WriteUint64(&stream, static_cast<uint64_t>(swap_bgbit));
    WriteUint64(&stream, static_cast<uint64_t>(neg_sk_l));
    WriteUint64(&stream, static_cast<uint64_t>(neg_sk_bgbit));
    WriteUint64(&stream, static_cast<uint64_t>(torus_bits));

    WriteUint64(&stream, substitution_keys.size());
    for (const auto& key : substitution_keys) {
        WriteBytes(&stream, key.Serialize());
    }

    WriteUint64(&stream, lwe_key_switch_keys.size());
    for (const auto& key : lwe_key_switch_keys) {
        WriteBytes(&stream, key.Serialize());
    }

    WriteUint64(&stream, recursive_ks_keys.size());
    for (const auto& key : recursive_ks_keys) {
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
    (void)tlwe_params;

    const std::string serialized(data.begin(), data.end());
    std::istringstream stream(serialized, std::ios::binary | std::ios::in);

    ExpansionBundle bundle;
    bundle.swap_l = static_cast<int32_t>(ReadUint64(&stream));
    bundle.swap_bgbit = static_cast<int32_t>(ReadUint64(&stream));
    bundle.neg_sk_l = static_cast<int32_t>(ReadUint64(&stream));
    bundle.neg_sk_bgbit = static_cast<int32_t>(ReadUint64(&stream));
    bundle.torus_bits = static_cast<int32_t>(ReadUint64(&stream));
    ValidateExpansionMetadata(bundle, config);

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

    const uint64_t recursive_key_count = ReadUint64(&stream);
    bundle.recursive_ks_keys.reserve(recursive_key_count);
    for (uint64_t i = 0; i < recursive_key_count; ++i) {
        bundle.recursive_ks_keys.emplace_back(
            RecursiveRlweKeySwitchKey::Deserialize(ReadBytes(&stream), tlwe_params));
    }

    bundle.neg_sk_rgsw_bytes = ReadBytes(&stream);
    return bundle;
}

TFHEContext::~TFHEContext() { Reset(); }

TFHEContext::TFHEContext(TFHEContext&& other) noexcept
    : tlwe_params(std::exchange(other.tlwe_params, nullptr)),
      swap_tgsw_params(std::exchange(other.swap_tgsw_params, nullptr)),
      neg_sk_tgsw_params(std::exchange(other.neg_sk_tgsw_params, nullptr)),
      practical_tgsw_params(std::exchange(other.practical_tgsw_params, nullptr)),
      tgsw_params(std::exchange(other.tgsw_params, nullptr)),
      tlwe_key(std::exchange(other.tlwe_key, nullptr)),
      swap_tgsw_key(std::exchange(other.swap_tgsw_key, nullptr)),
      neg_sk_tgsw_key(std::exchange(other.neg_sk_tgsw_key, nullptr)),
      practical_tgsw_key(std::exchange(other.practical_tgsw_key, nullptr)),
      tgsw_key(std::exchange(other.tgsw_key, nullptr)),
      rlwe_ks_basebit(other.rlwe_ks_basebit),
      rlwe_ks_length(other.rlwe_ks_length),
      plaintext_bits(other.plaintext_bits),
      alpha(other.alpha) {}

TFHEContext& TFHEContext::operator=(TFHEContext&& other) noexcept {
    if (this != &other) {
        Reset();
        tlwe_params = std::exchange(other.tlwe_params, nullptr);
        swap_tgsw_params = std::exchange(other.swap_tgsw_params, nullptr);
        neg_sk_tgsw_params = std::exchange(other.neg_sk_tgsw_params, nullptr);
        practical_tgsw_params = std::exchange(other.practical_tgsw_params, nullptr);
        tgsw_params = std::exchange(other.tgsw_params, nullptr);
        tlwe_key = std::exchange(other.tlwe_key, nullptr);
        swap_tgsw_key = std::exchange(other.swap_tgsw_key, nullptr);
        neg_sk_tgsw_key = std::exchange(other.neg_sk_tgsw_key, nullptr);
        practical_tgsw_key = std::exchange(other.practical_tgsw_key, nullptr);
        tgsw_key = std::exchange(other.tgsw_key, nullptr);
        rlwe_ks_basebit = other.rlwe_ks_basebit;
        rlwe_ks_length = other.rlwe_ks_length;
        plaintext_bits = other.plaintext_bits;
        alpha = other.alpha;
    }
    return *this;
}

TFHEContext TFHEContext::CreateClientContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.rlwe_ks_basebit = config.rlwe_ks_basebit;
    ctx.rlwe_ks_length = config.rlwe_ks_length;
    ctx.plaintext_bits = config.plaintext_bits;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.swap_tgsw_params =
        CreateTGswParams(config.swap_tgsw_l, config.swap_tgsw_bgbit, ctx.tlwe_params);
    ctx.neg_sk_tgsw_params =
        CreateTGswParams(config.neg_sk_tgsw_l, config.neg_sk_tgsw_bgbit, ctx.tlwe_params);
    ctx.practical_tgsw_params =
        CreateTGswParams(config.practical_tgsw_l, config.practical_tgsw_bgbit, ctx.tlwe_params);
    ctx.tgsw_params = ctx.swap_tgsw_params;

    ctx.swap_tgsw_key = new_TGswKey(ctx.swap_tgsw_params);
    tGswKeyGen(ctx.swap_tgsw_key);
    ctx.tlwe_key = &ctx.swap_tgsw_key->tlwe_key;

    ctx.neg_sk_tgsw_key = new_TGswKey(ctx.neg_sk_tgsw_params);
    CopyTlweSecretKey(&ctx.neg_sk_tgsw_key->tlwe_key, ctx.tlwe_key);
    ctx.practical_tgsw_key = new_TGswKey(ctx.practical_tgsw_params);
    CopyTlweSecretKey(&ctx.practical_tgsw_key->tlwe_key, ctx.tlwe_key);

    ctx.tgsw_key = ctx.swap_tgsw_key;
    return ctx;
}

TFHEContext TFHEContext::CreateServerContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.rlwe_ks_basebit = config.rlwe_ks_basebit;
    ctx.rlwe_ks_length = config.rlwe_ks_length;
    ctx.plaintext_bits = config.plaintext_bits;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.swap_tgsw_params =
        CreateTGswParams(config.swap_tgsw_l, config.swap_tgsw_bgbit, ctx.tlwe_params);
    ctx.neg_sk_tgsw_params =
        CreateTGswParams(config.neg_sk_tgsw_l, config.neg_sk_tgsw_bgbit, ctx.tlwe_params);
    ctx.practical_tgsw_params =
        CreateTGswParams(config.practical_tgsw_l, config.practical_tgsw_bgbit, ctx.tlwe_params);
    ctx.tgsw_params = ctx.swap_tgsw_params;
    return ctx;
}

void TFHEContext::Reset() {
    if (practical_tgsw_key != nullptr) {
        delete_TGswKey(practical_tgsw_key);
        practical_tgsw_key = nullptr;
    }
    if (neg_sk_tgsw_key != nullptr) {
        delete_TGswKey(neg_sk_tgsw_key);
        neg_sk_tgsw_key = nullptr;
    }
    if (swap_tgsw_key != nullptr) {
        delete_TGswKey(swap_tgsw_key);
        swap_tgsw_key = nullptr;
    }
    tlwe_key = nullptr;
    tgsw_key = nullptr;
    if (practical_tgsw_params != nullptr) {
        delete_TGswParams(practical_tgsw_params);
        practical_tgsw_params = nullptr;
    }
    if (neg_sk_tgsw_params != nullptr) {
        delete_TGswParams(neg_sk_tgsw_params);
        neg_sk_tgsw_params = nullptr;
    }
    if (swap_tgsw_params != nullptr) {
        delete_TGswParams(swap_tgsw_params);
        swap_tgsw_params = nullptr;
    }
    tgsw_params = nullptr;
    if (tlwe_params != nullptr) {
        delete_TLweParams(tlwe_params);
        tlwe_params = nullptr;
    }
    rlwe_ks_basebit = 0;
    rlwe_ks_length = 0;
    plaintext_bits = 0;
}

ExpansionBundle BuildExpansionBundle(const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    CheckClientTgswKey(ctx);

    ExpansionBundle bundle;
    PopulateExpansionMetadata(&bundle, ctx);

    size_t level_count = 0;
    for (int n = ctx.tlwe_params->N; n > 1; n >>= 1) {
        ++level_count;
    }

    bundle.substitution_keys.reserve(level_count);
    bundle.lwe_key_switch_keys.reserve(level_count);
    bundle.recursive_ks_keys.reserve(level_count);
    for (size_t level = 0; level < level_count; ++level) {
        bundle.substitution_keys.emplace_back(BuildSubstitutionMonomialCiphertext(
            ctx, ctx.tlwe_params->N >> static_cast<int>(level + 1)));
        bundle.lwe_key_switch_keys.emplace_back(BuildIdentityKeySwitchKey(ctx));
        const int32_t substitution_power =
            (ctx.tlwe_params->N >> static_cast<int>(level)) + 1;
        bundle.recursive_ks_keys.emplace_back(BuildRecursiveRlweKeySwitchKey(
            ctx, substitution_power, ctx.rlwe_ks_basebit, ctx.rlwe_ks_length));
    }

    bundle.neg_sk_rgsw_bytes = BuildNegatedSecretKeyCiphertext(ctx).Serialize();
    return bundle;
}

ExpansionBundle BuildRecursiveExpansionBundle(const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    CheckClientTgswKey(ctx);

    ExpansionBundle bundle;
    PopulateExpansionMetadata(&bundle, ctx);

    size_t level_count = 0;
    for (int n = ctx.tlwe_params->N; n > 1; n >>= 1) {
        ++level_count;
    }

    bundle.recursive_ks_keys.reserve(level_count);
    for (size_t level = 0; level < level_count; ++level) {
        const int32_t substitution_power =
            (ctx.tlwe_params->N >> static_cast<int>(level)) + 1;
        bundle.recursive_ks_keys.emplace_back(BuildRecursiveRlweKeySwitchKey(
            ctx, substitution_power, ctx.rlwe_ks_basebit, ctx.rlwe_ks_length));
    }

    bundle.neg_sk_rgsw_bytes = BuildNegatedSecretKeyCiphertext(ctx).Serialize();
    return bundle;
}

RGSWCiphertext EncryptNegatedSecretKey(const TFHEContext& ctx) {
    return BuildNegatedSecretKeyCiphertext(ctx);
}

RLWECiphertext EncryptBlock(const std::vector<uint8_t>& block, const TFHEContext& ctx) {
    CheckClientTlweKey(ctx);
    RLWECiphertext ciphertext(ctx.tlwe_params);
    TorusPolynomial* poly = EncodeBlock(block, ctx.tlwe_params, ctx.plaintext_bits);
    tLweSymEncrypt(ciphertext.Get(), poly, ctx.alpha, ctx.tlwe_key);
    delete_TorusPolynomial(poly);
    return ciphertext;
}

std::vector<uint8_t> DecryptBlock(const RLWECiphertext& ciphertext, const TFHEContext& ctx,
                                  size_t block_size) {
    CheckClientTlweKey(ctx);
    const size_t capacity_bytes =
        (static_cast<size_t>(ctx.tlwe_params->N) * static_cast<size_t>(ctx.plaintext_bits)) / 8;
    if (block_size > capacity_bytes) {
        throw std::invalid_argument("Requested block size exceeds TLWE polynomial plaintext capacity");
    }

    TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
    tLweSymDecrypt(poly, ciphertext.Get(), ctx.tlwe_key, PlaintextModulus(ctx.plaintext_bits));
    std::vector<uint8_t> block = DecodeBlock(poly, block_size, ctx.plaintext_bits);
    delete_TorusPolynomial(poly);
    return block;
}

RGSWCiphertext EncryptSwapBit(bool bit, const TFHEContext& ctx) {
    CheckClientSwapTgswKey(ctx);
    RGSWCiphertext ciphertext(ctx.swap_tgsw_params);
    tGswSymEncryptInt(ciphertext.Get(), bit ? 1 : 0, ctx.alpha, ctx.swap_tgsw_key);
    return ciphertext;
}

RGSWCiphertext EncryptPracticalBit(bool bit, const TFHEContext& ctx) {
    CheckClientPracticalTgswKey(ctx);
    RGSWCiphertext ciphertext(ctx.practical_tgsw_params);
    tGswSymEncryptInt(ciphertext.Get(), bit ? 1 : 0, ctx.alpha, ctx.practical_tgsw_key);
    return ciphertext;
}

RGSWCiphertext EncryptBit(bool bit, const TFHEContext& ctx) {
    return EncryptSwapBit(bit, ctx);
}

bool DecryptBit(const RGSWCiphertext& ciphertext, const TFHEContext& ctx) {
    CheckClientSwapTgswKey(ctx);
    const int ring_dimension = ctx.swap_tgsw_params->tlwe_params->N;
    IntPolynomial* poly = new_IntPolynomial(ring_dimension);
    tGswSymDecrypt(poly, ciphertext.Get(), ctx.swap_tgsw_key, 2);
    const bool bit = poly->coefs[0] != 0;
    delete_IntPolynomial(poly);
    return bit;
}

}  // namespace oram::onion_ring
