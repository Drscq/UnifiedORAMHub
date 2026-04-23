#include "oram/onion_ring/PermGen.h"

#include <algorithm>
#include <stdexcept>

#include "numeric_functions.h"
#include "polynomials.h"
#include "tlwe_functions.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {

namespace {

void AppendUint64(std::vector<uint8_t>* bytes, uint64_t value) {
    const uint8_t* value_bytes = reinterpret_cast<const uint8_t*>(&value);
    bytes->insert(bytes->end(), value_bytes, value_bytes + sizeof(value));
}

void AppendBlob(std::vector<uint8_t>* bytes, const std::vector<uint8_t>& blob) {
    AppendUint64(bytes, blob.size());
    bytes->insert(bytes->end(), blob.begin(), blob.end());
}

std::vector<uint8_t> EncryptPackedBits(const std::vector<bool>& swap_bits, size_t begin, size_t end,
                                       Torus32 torus_scale, const TFHEContext& ctx) {
    if (ctx.tlwe_key == nullptr || ctx.tlwe_params == nullptr) {
        throw std::invalid_argument("Recursive packed payload requires initialized TLWE key material");
    }

    RLWECiphertext ciphertext(ctx.tlwe_params);
    TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        poly->coefsT[coeff] = 0;
    }
    for (size_t bit_idx = begin; bit_idx < end; ++bit_idx) {
        poly->coefsT[bit_idx - begin] = swap_bits[bit_idx] ? torus_scale : 0;
    }
    tLweSymEncrypt(ciphertext.Get(), poly, ctx.alpha, ctx.tlwe_key);
    delete_TorusPolynomial(poly);
    return ciphertext.Serialize();
}

}  // namespace

SwapBitPayload BuildDirectSwapBitPayload(const std::vector<size_t>& permutation,
                                        const TFHEContext& ctx) {
    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);

    SwapBitPayload payload;
    payload.ciphertexts.reserve(swap_bits.size());
    for (bool bit : swap_bits) {
        payload.ciphertexts.push_back(EncryptSwapBit(bit, ctx).Serialize());
    }
    return payload;
}

PackedSwapBitPayload BuildPackedSwapBitPayload(const std::vector<size_t>& permutation,
                                              const TFHEContext& ctx) {
    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);
    constexpr size_t kBatchSize = 128;

    PackedSwapBitPayload payload;
    payload.mode = PackedSwapBitMode::kPracticalBatchedRgsw;
    payload.bit_count = swap_bits.size();
    payload.bits_per_ciphertext = kBatchSize;
    payload.row_count = 1;

    std::vector<uint8_t> batch;
    size_t count_in_batch = 0;
    for (bool bit : swap_bits) {
        AppendBlob(&batch, EncryptPracticalBit(bit, ctx).Serialize());
        ++count_in_batch;
        if (count_in_batch == kBatchSize) {
            payload.ciphertexts.push_back(std::move(batch));
            batch.clear();
            count_in_batch = 0;
        }
    }
    if (!batch.empty()) {
        payload.ciphertexts.push_back(std::move(batch));
    }

    return payload;
}

PackedSwapBitPayload BuildRecursivePackedSwapBitPayload(const std::vector<size_t>& permutation,
                                                       const TFHEContext& ctx) {
    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);

    PackedSwapBitPayload payload;
    payload.mode = PackedSwapBitMode::kRecursiveRlwe;
    payload.bit_count = swap_bits.size();
    payload.bits_per_ciphertext = static_cast<uint64_t>(ctx.tlwe_params != nullptr ? ctx.tlwe_params->N : 0);
    payload.row_count = static_cast<uint64_t>(ctx.swap_tgsw_params != nullptr ? ctx.swap_tgsw_params->l : 0);

    if (payload.bits_per_ciphertext == 0 || payload.row_count == 0) {
        throw std::invalid_argument("Recursive packed payload requires initialized TLWE and TGSW params");
    }

    for (size_t row = 0; row < payload.row_count; ++row) {
        const Torus32 torus_scale = ctx.swap_tgsw_params->h[row] / ctx.tlwe_params->N;
        if (torus_scale == 0) {
            throw std::invalid_argument(
                "Recursive packed payload requires TFHE parameters with enough Torus precision for h[row] / N");
        }
        for (size_t begin = 0; begin < swap_bits.size(); begin += payload.bits_per_ciphertext) {
            const size_t end = std::min(begin + static_cast<size_t>(payload.bits_per_ciphertext),
                                        swap_bits.size());
            payload.ciphertexts.push_back(EncryptPackedBits(swap_bits, begin, end, torus_scale, ctx));
        }
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
    const uint64_t mode = static_cast<uint64_t>(payload.mode);
    net_io->SendData(&mode, sizeof(mode));
    net_io->SendData(&payload.bit_count, sizeof(payload.bit_count));
    net_io->SendData(&payload.bits_per_ciphertext, sizeof(payload.bits_per_ciphertext));
    net_io->SendData(&payload.row_count, sizeof(payload.row_count));
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
    uint64_t mode = 0;
    uint64_t ciphertext_count = 0;
    net_io->RecvData(&mode, sizeof(mode));
    payload.mode = static_cast<PackedSwapBitMode>(mode);
    net_io->RecvData(&payload.bit_count, sizeof(payload.bit_count));
    net_io->RecvData(&payload.bits_per_ciphertext, sizeof(payload.bits_per_ciphertext));
    net_io->RecvData(&payload.row_count, sizeof(payload.row_count));
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
