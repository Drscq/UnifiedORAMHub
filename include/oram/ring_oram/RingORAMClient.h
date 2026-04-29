#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "oram/core/RAM.h"
#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"
#include "oram/ring_oram/Config.h"
#include "oram/ring_oram/RingBucket.h"

namespace oram::ring_oram {

class RingORAMClient : public core::RAM {
   public:
    RingORAMClient(const std::string& server_address, int port, const RuntimeConfig& config,
                   uint64_t seed = 0x434C49454E54ULL);
    ~RingORAMClient() override;

    std::vector<uint8_t> Access(core::Op op, uint64_t addr,
                                const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> Read(uint64_t addr) override;
    void Write(uint64_t addr, const std::vector<uint8_t>& data) override;

    std::optional<std::vector<uint8_t>> ReadPath(uint64_t leaf, uint64_t addr);
    void EvictPath();
    void EarlyReshuffle(uint64_t leaf);

    size_t GetBlockOffset(const RingBucket& bucket, uint64_t addr);
    void ReadBucket(size_t bucket_idx);
    void WriteBucket(size_t bucket_idx);

    bool CanResideInBucket(uint64_t block_leaf, size_t bucket_idx) const;

    const std::unordered_map<uint64_t, uint64_t>& PositionMap() const { return position_map_; }
    const std::vector<RingBlock>& Stash() const { return stash_; }
    size_t Round() const { return round_; }
    uint64_t EvictionCounter() const { return eviction_counter_; }
    size_t ReadPathCount() const { return read_path_count_; }
    size_t EarlyReshuffleCount() const { return early_reshuffle_count_; }
    uint64_t ServerBucketCount(size_t bucket_idx);
    uint64_t ServerXorPathReadCount();
    size_t ServerLastXorPathSlotCount();

   private:
    struct ServerStats {
        uint64_t xor_path_read_count = 0;
        size_t last_xor_path_slot_count = 0;
    };

    uint64_t GetRandomLeaf();
    void ValidateAddress(uint64_t addr) const;
    size_t GetBucketIndex(uint64_t leaf, size_t level) const;
    size_t GetNodeLevel(size_t bucket_idx) const;
    bool IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const;
    std::vector<uint8_t> XorBuffers(const std::vector<uint8_t>& lhs,
                                    const std::vector<uint8_t>& rhs) const;
    void XorInto(std::vector<uint8_t>* target, const std::vector<uint8_t>& source) const;
    void PutBlockInStash(const RingBlock& block);
    std::optional<RingBlock> TakeBlockFromStash(uint64_t addr);
    void RemoveBlockFromStash(uint64_t addr);
    std::vector<size_t> ValidRealOffsets(const RingBucket& bucket) const;
    std::vector<size_t> ValidDummyOffsets(const RingBucket& bucket) const;

    std::vector<size_t> GetPathIndices(uint64_t leaf) const;
    void InitializeServerStorage();
    std::vector<RingBucket> BuildInitialPlainTree();
    void FillBucketFromStash(size_t bucket_idx, RingBucket* bucket);

    EncryptedField EncryptBytes(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> DecryptBytes(const EncryptedField& field);
    EncryptedField EncryptUint64(uint64_t value);
    uint64_t DecryptUint64(const EncryptedField& field);
    EncryptedRingBucket EncryptBucket(const RingBucket& bucket);
    RingBucket DecryptBucket(const EncryptedRingBucket& encrypted);
    RingBucket DecryptMetadata(const RingBucketMetadata& metadata);

    std::vector<RingBucketMetadata> FetchPathMetadata(uint64_t leaf);
    std::optional<std::vector<uint8_t>> FetchXorPath(uint64_t leaf,
                                                     const std::vector<size_t>& offsets,
                                                     const std::vector<bool>& target_flags);
    EncryptedRingBucket ReadEncryptedBucketFromServer(size_t bucket_idx);
    void WriteEncryptedBucketToServer(size_t bucket_idx, const EncryptedRingBucket& bucket);
    ServerStats FetchServerStats();
    uint64_t FetchServerBucketCount(size_t bucket_idx);

    void ExpectAck();

    RuntimeConfig config_;
    std::unordered_map<uint64_t, uint64_t> position_map_;
    std::vector<RingBlock> stash_;
    size_t round_ = 0;
    uint64_t eviction_counter_ = 0;
    size_t read_path_count_ = 0;
    size_t early_reshuffle_count_ = 0;
    std::mt19937_64 prng_;
    std::unique_ptr<network::NetIO> net_io_;
    std::unique_ptr<crypto::AES_CTR> cipher_;
};

}  // namespace oram::ring_oram
