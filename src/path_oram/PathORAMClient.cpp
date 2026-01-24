#include "oram/path_oram/PathORAMClient.h"

#include <algorithm>
#include <iostream>
#include <random>

#include "oram/core/Serializer.h"

namespace oram::path_oram {

PathORAMClient::PathORAMClient(const std::string& server_address, int port, size_t tree_height,
                               size_t num_blocks)
    : tree_height_(tree_height),
      num_blocks_(num_blocks),
      num_leaves_(Config::GetNumLeaves(tree_height)) {
    // Initialize network (client mode)
    net_io_ = std::make_unique<network::NetIO>(server_address, port, false, false);

    // Initialize cipher with fixed shared key (for testing purposes)
    // In production, this wouldbe securely exchanged
    std::vector<uint8_t> shared_key(crypto::AES_CTR::kKeySize128, 0x42);
    cipher_ = std::make_unique<crypto::AES_CTR>(shared_key);

    // Initialize position map with random leaf assignments
    position_map_.resize(num_blocks);
    for (size_t i = 0; i < num_blocks; ++i) {
        position_map_[i] = GetRandomLeaf();
    }

    std::cout << "PathORAMClient initialized: height=" << tree_height << ", blocks=" << num_blocks
              << ", leaves=" << num_leaves_ << std::endl;
}

PathORAMClient::~PathORAMClient() {
    // Send quit command to server
    try {
        char quit_cmd = 'Q';
        net_io_->SendData(&quit_cmd, 1);
        net_io_->Flush();
    } catch (...) {
        // Ignore errors during shutdown
    }
}

void PathORAMClient::InitializePositionMap(const std::vector<uint64_t>& initial_positions) {
    if (initial_positions.size() != num_blocks_) {
        throw std::invalid_argument("Position map size mismatch");
    }
    position_map_ = initial_positions;
}

uint64_t PathORAMClient::GetRandomLeaf() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(0, num_leaves_ - 1);
    return dis(gen);
}

std::vector<uint8_t> PathORAMClient::Access(core::Op op, uint64_t addr,
                                            const std::vector<uint8_t>& data) {
    // Step 1: Look up current leaf position
    uint64_t old_leaf = position_map_[addr];

    // Step 2: Remap to new random leaf
    position_map_[addr] = GetRandomLeaf();

    // Step 3: Read path from server to stash
    ReadPath(old_leaf);

    // Step 4: Read/write block in stash
    std::vector<uint8_t> result;
    core::Block* block_ptr = FindBlockInStash(addr);

    if (op == core::Op::READ) {
        if (block_ptr) {
            result = block_ptr->GetData();
        } else {
            // Block not found - return empty data
            result.resize(Config::kBlockSize, 0);
        }
    } else {  // WRITE
        if (block_ptr) {
            // Update existing block
            RemoveBlockFromStash(addr);
        }
        // Add new/updated block to stash
        stash_.emplace_back(addr, data);
        result = data;
    }

    // Step 5: Write path back, evicting blocks from stash
    WritePath(old_leaf);

    return result;
}

std::vector<uint8_t> PathORAMClient::Read(uint64_t addr) {
    return Access(core::Op::READ, addr, {});
}

void PathORAMClient::Write(uint64_t addr, const std::vector<uint8_t>& data) {
    Access(core::Op::WRITE, addr, data);
}

std::vector<size_t> PathORAMClient::GetPathIndices(uint64_t leaf) {
    std::vector<size_t> indices;
    size_t leaf_offset = Config::GetLeafOffset(tree_height_);
    size_t current = leaf_offset + leaf;

    // Traverse from leaf to root
    while (true) {
        indices.push_back(current);
        if (current == 0) break;      // Reached root
        current = (current - 1) / 2;  // Parent in binary tree
    }

    return indices;
}

void PathORAMClient::ReadPath(uint64_t leaf) {
    auto path = GetPathIndices(leaf);

    // Read each bucket on the path
    for (size_t bucket_idx : path) {
        core::Bucket bucket = ReadBucketFromServer(bucket_idx);

        // Add all real blocks to stash
        for (const auto& block : bucket.GetBlocks()) {
            if (block.GetId() != ~0ULL) {  // Not a dummy block
                stash_.push_back(block);
            }
        }
    }
}

void PathORAMClient::WritePath(uint64_t leaf) {
    auto path = GetPathIndices(leaf);
    std::vector<core::Bucket> path_buckets;

    // Create empty buckets for the path
    for (size_t i = 0; i < path.size(); ++i) {
        path_buckets.emplace_back(Config::kBucketSize);
    }

    // Evict blocks from stash to path buckets
    EvictBlocksToPath(leaf, path_buckets);

    // Fill remaining space with dummy blocks
    for (auto& bucket : path_buckets) {
        while (bucket.GetBlockCount() < Config::kBucketSize) {
            std::vector<uint8_t> dummy_data(Config::kBlockSize, 0);
            core::Block dummy(~0ULL, dummy_data);
            bucket.AddBlock(dummy);
        }
    }

    // Write buckets back to server
    for (size_t i = 0; i < path.size(); ++i) {
        WriteBucketToServer(path[i], path_buckets[i]);
    }
}

void PathORAMClient::EvictBlocksToPath(uint64_t leaf, std::vector<core::Bucket>& path_buckets) {
    // Path ORAM eviction: try to place each block as DEEP as possible
    // path_buckets[0] = leaf bucket, path_buckets[L] = root bucket
    // Process from LEAF to ROOT to place blocks as deep as they can go

    auto stash_copy = stash_;
    stash_.clear();

    for (const auto& block : stash_copy) {
        uint64_t block_leaf = position_map_[block.GetId()];
        bool placed = false;

        // Try to place in deepest possible bucket (from leaf=0 to root=L)
        // Iterate path_buckets from index 0 (leaf) to size-1 (root)
        for (size_t i = 0; i < path_buckets.size() && !placed; ++i) {
            // At index i, we are at level i from the leaf
            // A block can be placed at this bucket if its path intersects here
            if (path_buckets[i].GetBlockCount() < Config::kBucketSize &&
                CanPlaceInBucket(block_leaf, leaf, i)) {
                path_buckets[i].AddBlock(block);
                placed = true;
            }
        }

        // If couldn't place, keep in stash
        if (!placed) {
            stash_.push_back(block);
        }
    }
}

bool PathORAMClient::CanPlaceInBucket(uint64_t block_leaf, uint64_t target_leaf, size_t level) {
    // Check if block_leaf's path intersects with target_leaf's path at this level
    // level 0 = leaf (most restrictive: only same leaf)
    // level L = root (least restrictive: all paths)
    //
    // At level i, paths share a common ancestor if their (tree_height_ - i) MSBs match
    // Equivalently, (leaf >> level) should be equal
    //
    // Example with height=3:
    //   level=0 (leaf): block_leaf >> 0 == target_leaf >> 0 (exact match required)
    //   level=1: block_leaf >> 1 == target_leaf >> 1 (share parent)
    //   level=2: block_leaf >> 2 == target_leaf >> 2 (share grandparent)
    //   level=3 (root): block_leaf >> 3 == target_leaf >> 3 (always 0==0, all match)

    return (block_leaf >> level) == (target_leaf >> level);
}

core::Block* PathORAMClient::FindBlockInStash(uint64_t block_id) {
    for (auto& block : stash_) {
        if (block.GetId() == block_id) {
            return &block;
        }
    }
    return nullptr;
}

void PathORAMClient::RemoveBlockFromStash(uint64_t block_id) {
    stash_.erase(std::remove_if(stash_.begin(), stash_.end(),
                                [block_id](const core::Block& b) { return b.GetId() == block_id; }),
                 stash_.end());
}

core::Bucket PathORAMClient::ReadBucketFromServer(size_t bucket_index) {
    // Send read command
    char cmd = 'R';
    net_io_->SendData(&cmd, 1);

    // Send bucket index
    uint64_t idx = bucket_index;
    net_io_->SendData(&idx, sizeof(idx));
    net_io_->Flush();

    // Receive size
    uint64_t size;
    net_io_->RecvData(&size, sizeof(size));

    // Receive encrypted bucket
    std::vector<uint8_t> encrypted(size);
    net_io_->RecvData(encrypted.data(), size);

    return DecryptBucket(encrypted);
}

void PathORAMClient::WriteBucketToServer(size_t bucket_index, const core::Bucket& bucket) {
    // Send write command
    char cmd = 'W';
    net_io_->SendData(&cmd, 1);

    // Send bucket index
    uint64_t idx = bucket_index;
    net_io_->SendData(&idx, sizeof(idx));

    // Encrypt bucket
    auto encrypted = EncryptBucket(bucket);

    // Send size
    uint64_t size = encrypted.size();
    net_io_->SendData(&size, sizeof(size));

    // Send encrypted data
    net_io_->SendData(encrypted.data(), encrypted.size());
    net_io_->Flush();
}

std::vector<uint8_t> PathORAMClient::EncryptBucket(const core::Bucket& bucket) {
    auto serialized = core::Serializer::SerializeBucket(bucket);
    auto iv = crypto::AES_CTR::GenerateIV();

    auto ciphertext = cipher_->Encrypt(serialized, iv);

    // Prepend IV
    std::vector<uint8_t> result;
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());

    return result;
}

core::Bucket PathORAMClient::DecryptBucket(const std::vector<uint8_t>& encrypted_data) {
    if (encrypted_data.size() < crypto::AES_CTR::kIVSize) {
        throw std::runtime_error("Invalid encrypted bucket data");
    }

    // Extract IV
    std::vector<uint8_t> iv(encrypted_data.begin(),
                            encrypted_data.begin() + crypto::AES_CTR::kIVSize);

    // Extract ciphertext
    std::vector<uint8_t> ciphertext(encrypted_data.begin() + crypto::AES_CTR::kIVSize,
                                    encrypted_data.end());

    // Decrypt
    auto plaintext = cipher_->Decrypt(ciphertext, iv);

    // Deserialize
    return core::Serializer::DeserializeBucket(plaintext, Config::kBucketSize);
}

}  // namespace oram::path_oram
