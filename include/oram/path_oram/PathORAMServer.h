#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "oram/core/Block.h"
#include "oram/core/Bucket.h"
#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"
#include "oram/path_oram/Config.h"

namespace oram::path_oram {

class PathORAMServer {
   public:
    struct RequestStats {
        size_t read_bucket_requests = 0;
        size_t write_bucket_requests = 0;
        size_t read_path_requests = 0;
        size_t write_path_requests = 0;
    };

    PathORAMServer(const std::string& address, int port,
                   size_t tree_height = Config::kDefaultTreeHeight,
                   const std::string& storage_dir = "");
    ~PathORAMServer();

    // Initialize the tree with real blocks placed in leaves
    void Init(const std::vector<core::Block>& initial_blocks);

    // Main server loop - handle client requests
    void HandleRequests();

    // Stop the server
    void Stop();

    RequestStats GetRequestStats() const;

   private:
    // Server state
    size_t tree_height_;
    size_t num_nodes_;
    size_t num_leaves_;
    size_t node_count_;
    std::string storage_dir_;
    std::string tree_dir_;

    // Network and crypto
    std::unique_ptr<network::NetIO> net_io_;
    std::unique_ptr<crypto::AES_CTR> cipher_;

    // Server control
    bool running_;
    RequestStats request_stats_;

    // Helper methods
    void FillBucketWithDummies(core::Bucket& bucket);
    void ValidateInitialized() const;
    void ValidateBucketShape(const core::Bucket& bucket, const std::string& context) const;
    std::string BucketPath(size_t bucket_index) const;
    core::Bucket ReadBucketFromDisk(size_t bucket_index) const;
    void WriteBucketToDisk(size_t bucket_index, const core::Bucket& bucket) const;
    std::vector<uint8_t> EncryptBucket(const core::Bucket& bucket);
    core::Bucket DecryptBucket(const std::vector<uint8_t>& encrypted_data);

    // Protocol handlers
    void HandleReadBucket(size_t bucket_index);
    void HandleWriteBucket(size_t bucket_index);
    void HandleReadPath();
    void HandleWritePath();
};

}  // namespace oram::path_oram
