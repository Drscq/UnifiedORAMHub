#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "oram/network/NetIO.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

struct SwapBitPayload {
    std::vector<std::vector<uint8_t>> ciphertexts;

    size_t BitCount() const { return ciphertexts.size(); }
};

SwapBitPayload BuildDirectSwapBitPayload(const std::vector<size_t>& permutation,
                                        const TFHEContext& ctx);

std::vector<RGSWCiphertext> DeserializeDirectSwapBitPayload(const SwapBitPayload& payload,
                                                            const TGswParams* params);

void SendDirectSwapBitPayload(network::NetIO* net_io, const SwapBitPayload& payload);

SwapBitPayload RecvDirectSwapBitPayload(network::NetIO* net_io);

}  // namespace oram::onion_ring
