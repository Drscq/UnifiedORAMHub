#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "oram/core/Block.h"
#include "oram/core/Bucket.h"
#include "oram/core/RAM.h"
#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"
#include "oram/path_oram/Config.h"

namespace oram::path_oram {

class PathORAMClient : public core::RAM {
   public:
    PathORAMClient(const std::string& server_address, int port,
                   size_t tree_height = Config::kDefaultTreeHeight,
                   size_t num_blocks = Config::kDefaultNumBlocks);
    ~PathORAMClient();

    // RAM interface implementation
    std::vector<uint8_t> Access(core::Op op, uint64_t addr,
                                const std::vector<uint8_t>& data) override;

    std::vector<uint8_t> Read(uint64_t addr) override;
    void Write(uint64_t addr, const std::vector<uint8_t>& data) override;

    // Initialize position map (call after server is ready)
    void InitializePositionMap(const std::vector<uint64_t>& initial_positions);

   private:
    // Client state
    size_t tree_height_;
    size_t num_blocks_;
    size_t num_leaves_;
    std::vector<core::Block> stash_;
    std::vector<uint64_t> position_map_;  // block_id -> leaf_id

    // Network and crypto
    std::unique_ptr<network::NetIO> net_io_;
    std::unique_ptr<crypto::AES_CTR> cipher_;

    // Helper methods
    uint64_t GetRandomLeaf();
    void ReadPath(uint64_t leaf);
    void WritePath(uint64_t leaf);
    core::Bucket ReadBucketFromServer(size_t bucket_index);
    void WriteBucketToServer(size_t bucket_index, const core::Bucket& bucket);

    // Stash management
    core::Block* FindBlockInStash(uint64_t block_id);
    void RemoveBlockFromStash(uint64_t block_id);

    // Eviction algorithm
    void EvictBlocksToPath(uint64_t leaf, std::vector<core::Bucket>& path_buckets);
    bool CanPlaceInBucket(uint64_t block_leaf, uint64_t target_leaf, size_t level);

    // Tree navigation
    size_t GetBucketIndex(uint64_t leaf, size_t level);
    std::vector<size_t> GetPathIndices(uint64_t leaf);

    // Encryption/Decryption helpers
    std::vector<uint8_t> EncryptBucket(const core::Bucket& bucket);
    core::Bucket DecryptBucket(const std::vector<uint8_t>& encrypted_data);
};

}  // namespace oram::path_oram
