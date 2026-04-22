#include "oram/onion_ring/PermGen.h"

#include <stdexcept>

#include "tlwe_functions.h"
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

PackedSwapBitPayload BuildPackedSwapBitPayload(const std::vector<size_t>& permutation,
                                              const TFHEContext& ctx) {
    if (ctx.tlwe_key == nullptr) {
        throw std::runtime_error("Packed swap-bit payload generation requires a client TLWE key");
    }

    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);

    PackedSwapBitPayload payload;
    payload.bit_count = swap_bits.size();

    const size_t coeffs_per_ciphertext = static_cast<size_t>(ctx.tlwe_params->N);
    for (size_t offset = 0; offset < swap_bits.size(); offset += coeffs_per_ciphertext) {
        RLWECiphertext packed(ctx.tlwe_params);
        TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
        torusPolynomialClear(poly);

        const size_t chunk_size = std::min(coeffs_per_ciphertext, swap_bits.size() - offset);
        for (size_t i = 0; i < chunk_size; ++i) {
            poly->coefsT[i] = modSwitchToTorus32(swap_bits[offset + i] ? 1 : 0, 2);
        }

        tLweSymEncrypt(packed.Get(), poly, ctx.alpha, ctx.tlwe_key);
        delete_TorusPolynomial(poly);
        payload.ciphertexts.push_back(packed.Serialize());
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

void SendPackedSwapBitPayload(network::NetIO* net_io, const PackedSwapBitPayload& payload) {
    if (net_io == nullptr) {
        throw std::invalid_argument("Packed swap-bit transport requires a live NetIO channel");
    }

    const uint64_t ciphertext_count = payload.ciphertexts.size();
    net_io->SendData(&payload.bit_count, sizeof(payload.bit_count));
    net_io->SendData(&ciphertext_count, sizeof(ciphertext_count));
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

PackedSwapBitPayload RecvPackedSwapBitPayload(network::NetIO* net_io) {
    if (net_io == nullptr) {
        throw std::invalid_argument("Packed swap-bit transport requires a live NetIO channel");
    }

    PackedSwapBitPayload payload;
    uint64_t ciphertext_count = 0;
    net_io->RecvData(&payload.bit_count, sizeof(payload.bit_count));
    net_io->RecvData(&ciphertext_count, sizeof(ciphertext_count));

    payload.ciphertexts.reserve(ciphertext_count);
    for (uint64_t i = 0; i < ciphertext_count; ++i) {
        std::vector<uint8_t> ciphertext;
        net_io->RecvVec(ciphertext);
        payload.ciphertexts.push_back(std::move(ciphertext));
    }
    return payload;
}

}  // namespace oram::onion_ring
