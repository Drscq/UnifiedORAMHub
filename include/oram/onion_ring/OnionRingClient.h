#pragma once

#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "oram/core/RAM.h"
#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"
#include "oram/onion_ring/Config.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

class OnionRingClient : public core::RAM {
   public:
    OnionRingClient(const std::string& server_address, int port, const RuntimeConfig& config);
    ~OnionRingClient() override;

    std::vector<uint8_t> Access(core::Op op, uint64_t addr,
                                const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> Read(uint64_t addr) override;
    void Write(uint64_t addr, const std::vector<uint8_t>& data) override;

    static uint64_t ReverseBits(uint64_t value, size_t num_bits);

   private:
    RuntimeConfig config_;
    size_t num_nodes_;
    size_t num_leaves_;
    std::vector<uint64_t> pos_map_;
    std::vector<std::vector<int64_t>> id_map_;
    TFHEContext ctx_;
    std::unique_ptr<network::NetIO> net_io_;
    std::unique_ptr<crypto::AES_CTR> cipher_;
    std::vector<uint8_t> expansion_bundle_bytes_;
    size_t access_count_ = 0;
    size_t eviction_counter_ = 0;
    std::mt19937_64 prng_;

    uint64_t GetRandomLeaf();
    std::vector<size_t> GetPathIndices(uint64_t leaf) const;
    size_t GetNodeLevel(size_t bucket_idx) const;
    size_t FindSlot(size_t bucket_idx, int64_t block_id) const;
    size_t FindFirstDummySlot(size_t bucket_idx) const;
    RLWECiphertext FetchPathSum(uint64_t leaf, const std::vector<size_t>& selections);
    void ClearPathSlots(uint64_t leaf, const std::vector<size_t>& selections);
    void WriteBackSlot(size_t bucket_idx, size_t slot_idx, const RLWECiphertext& ciphertext);
    void Evict();
    void TripletEvict(size_t source_idx, size_t left_idx, size_t right_idx);
    void LeafRefresh(size_t leaf_bucket_idx);
};

}  // namespace oram::onion_ring
