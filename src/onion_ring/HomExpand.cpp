#include "oram/onion_ring/HomExpand.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

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

}  // namespace

std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx) {
    if (server_ctx.tgsw_params == nullptr) {
        throw std::invalid_argument("HomExpandPackedSwapBits requires initialized server params");
    }
    (void)bundle;

    std::vector<RGSWCiphertext> expanded;
    expanded.reserve(payload.bit_count);

    for (const auto& batch : payload.ciphertexts) {
        const auto ciphertexts = ParseBatch(batch, 0);
        for (const auto& bytes : ciphertexts) {
            if (expanded.size() == payload.bit_count) {
                throw std::runtime_error("Packed HomExpand received more controls than expected");
            }
            expanded.emplace_back(RGSWCiphertext::Deserialize(bytes, server_ctx.tgsw_params));
        }
    }

    if (expanded.size() != payload.bit_count) {
        throw std::runtime_error("Packed HomExpand did not recover the expected number of controls");
    }
    return expanded;
}

}  // namespace oram::onion_ring
