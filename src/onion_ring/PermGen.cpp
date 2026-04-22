#include "oram/onion_ring/PermGen.h"

#include <stdexcept>

#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {

SwapBitPayload BuildDirectSwapBitPayload(const std::vector<size_t>& permutation,
                                        const TFHEContext& ctx) {
    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);

    SwapBitPayload payload;
    payload.ciphertexts.reserve(swap_bits.size());
    for (bool bit : swap_bits) {
        payload.ciphertexts.push_back(EncryptBit(bit, ctx).Serialize());
    }
    return payload;
}

std::vector<RGSWCiphertext> DeserializeDirectSwapBitPayload(const SwapBitPayload& payload,
                                                            const TGswParams* params) {
    std::vector<RGSWCiphertext> swap_bits;
    swap_bits.reserve(payload.ciphertexts.size());
    for (const auto& ciphertext : payload.ciphertexts) {
        swap_bits.emplace_back(RGSWCiphertext::Deserialize(ciphertext, params));
    }
    return swap_bits;
}

void SendDirectSwapBitPayload(network::NetIO* net_io, const SwapBitPayload& payload) {
    if (net_io == nullptr) {
        throw std::invalid_argument("Swap-bit transport requires a live NetIO channel");
    }

    uint64_t bit_count = payload.BitCount();
    net_io->SendData(&bit_count, sizeof(bit_count));
    for (const auto& ciphertext : payload.ciphertexts) {
        net_io->SendVec(ciphertext);
    }
}

SwapBitPayload RecvDirectSwapBitPayload(network::NetIO* net_io) {
    if (net_io == nullptr) {
        throw std::invalid_argument("Swap-bit transport requires a live NetIO channel");
    }

    uint64_t bit_count = 0;
    net_io->RecvData(&bit_count, sizeof(bit_count));

    SwapBitPayload payload;
    payload.ciphertexts.reserve(bit_count);
    for (uint64_t i = 0; i < bit_count; ++i) {
        std::vector<uint8_t> ciphertext;
        net_io->RecvVec(ciphertext);
        payload.ciphertexts.push_back(std::move(ciphertext));
    }
    return payload;
}

}  // namespace oram::onion_ring
