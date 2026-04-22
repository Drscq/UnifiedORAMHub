#include "oram/onion_ring/PermGen.h"

#include <stdexcept>

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

}  // namespace

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
    WaksmanNetwork network(permutation.size());
    const std::vector<bool> swap_bits = network.GenerateSwapBits(permutation);

    PackedSwapBitPayload payload;
    payload.bit_count = swap_bits.size();

    constexpr size_t kBatchSize = 128;
    std::vector<uint8_t> batch;
    size_t count_in_batch = 0;
    for (bool bit : swap_bits) {
        AppendBlob(&batch, EncryptBit(bit, ctx).Serialize());
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
