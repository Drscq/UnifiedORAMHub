#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "oram/network/NetIO.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

enum class PackedSwapBitMode : uint64_t {
    kPracticalBatchedRgsw = 0,
    kRecursiveRlwe = 1,
};

struct SwapBitPayload {
    std::vector<std::vector<uint8_t>> ciphertexts;

    size_t BitCount() const { return ciphertexts.size(); }
};

struct PackedSwapBitPayload {
    PackedSwapBitMode mode = PackedSwapBitMode::kPracticalBatchedRgsw;
    uint64_t bit_count = 0;
    uint64_t bits_per_ciphertext = 0;
    uint64_t row_count = 1;
    std::vector<std::vector<uint8_t>> ciphertexts;
};

SwapBitPayload BuildDirectSwapBitPayload(const std::vector<size_t>& permutation,
                                        const TFHEContext& ctx);
PackedSwapBitPayload BuildPackedSwapBitPayload(const std::vector<size_t>& permutation,
                                              const TFHEContext& ctx);
PackedSwapBitPayload BuildRecursivePackedSwapBitPayload(const std::vector<size_t>& permutation,
                                                       const TFHEContext& ctx);

std::vector<RGSWCiphertext> DeserializeDirectSwapBitPayload(const SwapBitPayload& payload,
                                                            const TGswParams* params);

void SendDirectSwapBitPayload(network::NetIO* net_io, const SwapBitPayload& payload);
void SendPackedSwapBitPayload(network::NetIO* net_io, const PackedSwapBitPayload& payload);

SwapBitPayload RecvDirectSwapBitPayload(network::NetIO* net_io);
PackedSwapBitPayload RecvPackedSwapBitPayload(network::NetIO* net_io);

}  // namespace oram::onion_ring
