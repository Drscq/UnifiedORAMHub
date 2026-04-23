#include "oram/onion_ring/HomExpand.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "polynomials.h"
#include "polynomials_arithmetic.h"
#include "tgsw_functions.h"
#include "tlwe_functions.h"

#include "oram/onion_ring/HomOps.h"

namespace oram::onion_ring {

namespace {

uint64_t ReadUint64(const std::vector<uint8_t>& bytes, size_t* cursor) {
    if (cursor == nullptr || *cursor + sizeof(uint64_t) > bytes.size()) {
        throw std::runtime_error("Packed HomExpand encountered a truncated batch header");
    }

    uint64_t value = 0;
    std::memcpy(&value, bytes.data() + *cursor, sizeof(value));
    *cursor += sizeof(value);
    return value;
}

std::vector<std::vector<uint8_t>> ParseBatch(const std::vector<uint8_t>& batch, size_t expected_count) {
    std::vector<std::vector<uint8_t>> rows;
    if (expected_count > 0) {
        rows.reserve(expected_count);
    }

    size_t cursor = 0;
    while (cursor < batch.size()) {
        const uint64_t blob_size = ReadUint64(batch, &cursor);
        if (cursor + blob_size > batch.size()) {
            throw std::runtime_error("Packed HomExpand encountered a truncated RLWE row");
        }
        rows.emplace_back(batch.begin() + static_cast<std::ptrdiff_t>(cursor),
                          batch.begin() + static_cast<std::ptrdiff_t>(cursor + blob_size));
        cursor += blob_size;
    }

    if (expected_count > 0 && rows.size() != expected_count) {
        throw std::runtime_error("Packed HomExpand batch did not contain the expected RLWE row count");
    }
    return rows;
}

size_t ChunkCount(const PackedSwapBitPayload& payload) {
    if (payload.bits_per_ciphertext == 0) {
        throw std::invalid_argument("Packed HomExpand requires a non-zero bits_per_ciphertext");
    }
    return static_cast<size_t>((payload.bit_count + payload.bits_per_ciphertext - 1) /
                               payload.bits_per_ciphertext);
}

size_t ReverseBits(size_t value, size_t bit_count) {
    size_t reversed = 0;
    for (size_t bit = 0; bit < bit_count; ++bit) {
        reversed = (reversed << 1) | ((value >> bit) & 1ULL);
    }
    return reversed;
}

const RecursiveRlweKeySwitchKey& LookupRecursiveKey(const ExpansionBundle& bundle, int32_t power) {
    for (const auto& key : bundle.recursive_ks_keys) {
        if (key.substitution_power == power) {
            return key;
        }
    }
    throw std::runtime_error("Recursive HomExpand is missing a substitution key-switch key");
}

void MultiplyByMonomial(TLweSample* result, const TLweSample* input, int32_t power,
                        const TLweParams* params) {
    for (int poly = 0; poly <= params->k; ++poly) {
        torusPolynomialMulByXai(&result->a[poly], power, &input->a[poly]);
    }
    result->current_variance = input->current_variance;
}

void KeySwitchRlwe(TLweSample* result, const TLweSample* input,
                   const RecursiveRlweKeySwitchKey& key, const TLweParams* params) {
    if (params->k != 1) {
        throw std::invalid_argument("Recursive HomExpand currently expects TLWE k == 1");
    }
    if (key.levels.empty()) {
        throw std::invalid_argument("Recursive HomExpand requires non-empty RLWE key-switch rows");
    }
    if (key.basebit <= 0 || key.basebit * static_cast<int32_t>(key.levels.size()) > 31) {
        throw std::invalid_argument("Recursive HomExpand key-switch parameters are not Torus32-compatible");
    }

    TGswParams* decomp_params = new_TGswParams(static_cast<int>(key.levels.size()), key.basebit, params);
    IntPolynomial* digits = new_IntPolynomial_array(static_cast<int>(key.levels.size()), params->N);
    IntPolynomial* neg_digits = new_IntPolynomial(params->N);
    tGswTorus32PolynomialDecompH(digits, &input->a[0], decomp_params);

    tLweNoiselessTrivial(result, input->b, params);
    for (size_t level = 0; level < key.levels.size(); ++level) {
        for (int coeff = 0; coeff < params->N; ++coeff) {
            neg_digits->coefs[coeff] = -digits[level].coefs[coeff];
        }
        tLweAddMulRTo(result, neg_digits, key.levels[level].Get(), params);
    }

    delete_IntPolynomial(neg_digits);
    delete_IntPolynomial_array(static_cast<int>(key.levels.size()), digits);
    delete_TGswParams(decomp_params);
}

RLWECiphertext SubstituteAndKeySwitch(const RLWECiphertext& input,
                                      const RecursiveRlweKeySwitchKey& key,
                                      const TLweParams* params) {
    RLWECiphertext substituted(params);
    Subs(substituted.Get(), input.Get(), key.substitution_power, params);

    RLWECiphertext keyed(params);
    KeySwitchRlwe(keyed.Get(), substituted.Get(), key, params);
    return keyed;
}

std::vector<RLWECiphertext> ExpandSinglePackedCiphertext(const RLWECiphertext& packed,
                                                         const ExpansionBundle& bundle,
                                                         const TFHEContext& server_ctx) {
    if (bundle.recursive_ks_keys.empty()) {
        throw std::invalid_argument("Recursive HomExpand requires recursive key-switch material");
    }

    std::vector<RLWECiphertext> current;
    current.emplace_back(server_ctx.tlwe_params);
    tLweCopy(current.back().Get(), packed.Get(), server_ctx.tlwe_params);

    for (size_t level = 0; level < bundle.recursive_ks_keys.size(); ++level) {
        const int32_t power =
            (server_ctx.tlwe_params->N >> static_cast<int>(level)) + 1;
        const auto& key = LookupRecursiveKey(bundle, power);

        std::vector<RLWECiphertext> next;
        next.reserve(current.size() * 2);
        for (const auto& sample : current) {
            RLWECiphertext substituted =
                SubstituteAndKeySwitch(sample, key, server_ctx.tlwe_params);

            RLWECiphertext even(server_ctx.tlwe_params);
            tLweCopy(even.Get(), sample.Get(), server_ctx.tlwe_params);
            tLweAddTo(even.Get(), substituted.Get(), server_ctx.tlwe_params);

            RLWECiphertext odd_delta(server_ctx.tlwe_params);
            tLweCopy(odd_delta.Get(), sample.Get(), server_ctx.tlwe_params);
            tLweSubTo(odd_delta.Get(), substituted.Get(), server_ctx.tlwe_params);

            RLWECiphertext odd(server_ctx.tlwe_params);
            const int32_t odd_shift = 1 << static_cast<int>(level);
            MultiplyByMonomial(odd.Get(), odd_delta.Get(),
                               2 * server_ctx.tlwe_params->N - odd_shift, server_ctx.tlwe_params);

            next.emplace_back(std::move(even));
            next.emplace_back(std::move(odd));
        }
        current = std::move(next);
    }

    return current;
}

std::vector<RLWECiphertext> ExpandPackedRlweRow(const PackedSwapBitPayload& payload,
                                                const ExpansionBundle& bundle,
                                                const TFHEContext& server_ctx,
                                                size_t row_index) {
    if (payload.mode != PackedSwapBitMode::kRecursiveRlwe) {
        throw std::invalid_argument("ExpandPackedRlweRow requires a recursive RLWE payload");
    }
    if (row_index >= payload.row_count) {
        throw std::out_of_range("Recursive HomExpand row index out of range");
    }

    const size_t chunk_count = ChunkCount(payload);
    if (payload.ciphertexts.size() != payload.row_count * chunk_count) {
        throw std::runtime_error("Recursive HomExpand payload shape does not match row/chunk metadata");
    }

    std::vector<RLWECiphertext> expanded;
    expanded.reserve(payload.bit_count);
    const size_t depth = bundle.recursive_ks_keys.size();
    for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
        const size_t payload_index = row_index * chunk_count + chunk;
        RLWECiphertext packed =
            RLWECiphertext::Deserialize(payload.ciphertexts[payload_index], server_ctx.tlwe_params);
        auto isolated = ExpandSinglePackedCiphertext(packed, bundle, server_ctx);

        const size_t expected = std::min(static_cast<size_t>(payload.bits_per_ciphertext),
                                         static_cast<size_t>(payload.bit_count) -
                                             chunk * static_cast<size_t>(payload.bits_per_ciphertext));
        if (isolated.size() < expected) {
            throw std::runtime_error("Recursive HomExpand produced fewer isolated coefficients than expected");
        }
        for (size_t i = 0; i < expected; ++i) {
            const size_t source_index = ReverseBits(i, depth);
            if (source_index >= isolated.size()) {
                throw std::runtime_error("Recursive HomExpand bit-reversal index exceeded isolated output size");
            }
            expanded.emplace_back(std::move(isolated[source_index]));
        }
    }

    return expanded;
}

std::vector<RGSWCiphertext> DeserializePracticalPackedSwapBits(const PackedSwapBitPayload& payload,
                                                               const TFHEContext& server_ctx) {
    std::vector<RGSWCiphertext> expanded;
    expanded.reserve(payload.bit_count);

    for (const auto& batch : payload.ciphertexts) {
        const auto ciphertexts = ParseBatch(batch, 0);
        for (const auto& bytes : ciphertexts) {
            if (expanded.size() == payload.bit_count) {
                throw std::runtime_error("Packed HomExpand received more controls than expected");
            }
            expanded.emplace_back(RGSWCiphertext::Deserialize(bytes, server_ctx.practical_tgsw_params));
        }
    }

    if (expanded.size() != payload.bit_count) {
        throw std::runtime_error("Packed HomExpand did not recover the expected number of controls");
    }
    return expanded;
}

std::vector<RGSWCiphertext> LiftExpandedRlweRows(const std::vector<std::vector<RLWECiphertext>>& rows,
                                                 const ExpansionBundle& bundle,
                                                 const TFHEContext& server_ctx) {
    if (bundle.neg_sk_rgsw_bytes.empty()) {
        throw std::invalid_argument("Recursive HomExpand requires serialized RGSW(-s) support material");
    }
    if (rows.empty()) {
        return {};
    }
    if (rows.size() != static_cast<size_t>(server_ctx.swap_tgsw_params->l)) {
        throw std::invalid_argument(
            "Recursive HomExpand row count must match the swap TGSW decomposition length");
    }

    const size_t bit_count = rows.front().size();
    for (const auto& row : rows) {
        if (row.size() != bit_count) {
            throw std::invalid_argument("Recursive HomExpand rows must agree on the gate count");
        }
    }

    RGSWCiphertext neg_secret =
        RGSWCiphertext::Deserialize(bundle.neg_sk_rgsw_bytes, server_ctx.neg_sk_tgsw_params);

    std::vector<RGSWCiphertext> lifted;
    lifted.reserve(bit_count);
    for (size_t bit = 0; bit < bit_count; ++bit) {
        RGSWCiphertext control(server_ctx.swap_tgsw_params);
        tGswClear(control.Get(), server_ctx.swap_tgsw_params);

        for (int row = 0; row < server_ctx.swap_tgsw_params->l; ++row) {
            const TLweSample* source = rows[static_cast<size_t>(row)][bit].Get();
            ExternalProductWithParams(&control.Get()->all_sample[row], neg_secret.Get(), source,
                                      server_ctx.neg_sk_tgsw_params);
            tLweCopy(&control.Get()->all_sample[server_ctx.swap_tgsw_params->l + row], source,
                     server_ctx.tlwe_params);
        }

        lifted.emplace_back(std::move(control));
    }
    return lifted;
}

}  // namespace

std::vector<RLWECiphertext> ExpandPackedRlweForTest(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx) {
    if (server_ctx.tlwe_params == nullptr) {
        throw std::invalid_argument("ExpandPackedRlweForTest requires initialized server TLWE params");
    }
    return ExpandPackedRlweRow(payload, bundle, server_ctx, 0);
}

std::vector<RLWECiphertext> ExpandPackedRlweRowForTest(const PackedSwapBitPayload& payload,
                                                       const ExpansionBundle& bundle,
                                                       const TFHEContext& server_ctx,
                                                       size_t row_index) {
    if (server_ctx.tlwe_params == nullptr) {
        throw std::invalid_argument(
            "ExpandPackedRlweRowForTest requires initialized server TLWE params");
    }
    return ExpandPackedRlweRow(payload, bundle, server_ctx, row_index);
}

RLWECiphertext ApplyRecursiveSubstitutionKeySwitchForTest(const RLWECiphertext& input,
                                                          const ExpansionBundle& bundle,
                                                          const TFHEContext& server_ctx,
                                                          int32_t substitution_power) {
    if (server_ctx.tlwe_params == nullptr) {
        throw std::invalid_argument(
            "ApplyRecursiveSubstitutionKeySwitchForTest requires initialized server TLWE params");
    }
    return SubstituteAndKeySwitch(input, LookupRecursiveKey(bundle, substitution_power),
                                  server_ctx.tlwe_params);
}

std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx) {
    if (server_ctx.swap_tgsw_params == nullptr || server_ctx.practical_tgsw_params == nullptr) {
        throw std::invalid_argument("HomExpandPackedSwapBits requires initialized server params");
    }

    if (payload.mode == PackedSwapBitMode::kRecursiveRlwe) {
        std::vector<std::vector<RLWECiphertext>> rows;
        rows.reserve(payload.row_count);
        for (size_t row = 0; row < payload.row_count; ++row) {
            rows.push_back(ExpandPackedRlweRow(payload, bundle, server_ctx, row));
        }
        return LiftExpandedRlweRows(rows, bundle, server_ctx);
    }

    return DeserializePracticalPackedSwapBits(payload, server_ctx);
}

}  // namespace oram::onion_ring
