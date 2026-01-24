#pragma once

#include <memory>
#include <vector>

#include "oram/core/Block.h"
#include "oram/core/Bucket.h"
#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"
#include "oram/path_oram/Config.h"

namespace oram::path_oram {

class PathORAMServer {
   public:
    PathORAMServer(const std::string& address, int port,
                   size_t tree_height = Config::kDefaultTreeHeight);
    ~PathORAMServer();

    // Initialize the tree with real blocks placed in leaves
    void Init(const std::vector<core::Block>& initial_blocks);

    // Main server loop - handle client requests
    void HandleRequests();

    // Stop the server
    void Stop();

   private:
    // Server state
    size_t tree_height_;
    size_t num_nodes_;
    size_t num_leaves_;
    std::vector<core::Bucket> tree_;

    // Network and crypto
    std::unique_ptr<network::NetIO> net_io_;
    std::unique_ptr<crypto::AES_CTR> cipher_;

    // Server control
    bool running_;

    // Helper methods
    void InitializeTreeWithDummies();
    void PlaceBlocksInLeaves(const std::vector<core::Block>& blocks);
    std::vector<uint8_t> EncryptBucket(const core::Bucket& bucket);
    core::Bucket DecryptBucket(const std::vector<uint8_t>& encrypted_data);

    // Protocol handlers
    void HandleReadBucket(size_t bucket_index);
    void HandleWriteBucket(size_t bucket_index);
};

}  // namespace oram::path_oram
