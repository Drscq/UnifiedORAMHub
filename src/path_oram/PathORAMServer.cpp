#include "oram/path_oram/PathORAMServer.h"

#include <iostream>
#include <stdexcept>

#include "oram/core/Serializer.h"

namespace oram::path_oram {

PathORAMServer::PathORAMServer(const std::string& address, int port, size_t tree_height)
    : tree_height_(tree_height),
      num_nodes_(Config::GetNumTreeNodes(tree_height)),
      num_leaves_(Config::GetNumLeaves(tree_height)),
      running_(false) {
    // Initialize network (server mode)
    net_io_ = std::make_unique<network::NetIO>(address, port, true, false);

    // Initialize cipher with fixed shared key (for testing purposes)
    std::vector<uint8_t> shared_key(crypto::AES_CTR::kKeySize128, 0x42);
    cipher_ = std::make_unique<crypto::AES_CTR>(shared_key);

    std::cout << "PathORAMServer initialized: height=" << tree_height << ", nodes=" << num_nodes_
              << ", leaves=" << num_leaves_ << std::endl;
}

PathORAMServer::~PathORAMServer() { Stop(); }

void PathORAMServer::Init(const std::vector<core::Block>& initial_blocks) {
    if (initial_blocks.size() > num_leaves_) {
        throw std::invalid_argument("Too many blocks for tree capacity");
    }

    // Initialize all buckets
    tree_.clear();
    tree_.reserve(num_nodes_);
    for (size_t i = 0; i < num_nodes_; ++i) {
        tree_.emplace_back(Config::kBucketSize);
    }

    // Place real blocks in leaf nodes
    PlaceBlocksInLeaves(initial_blocks);

    // Fill remaining space with dummy blocks
    InitializeTreeWithDummies();

    std::cout << "Server initialized with " << initial_blocks.size() << " real blocks" << std::endl;
}

void PathORAMServer::PlaceBlocksInLeaves(const std::vector<core::Block>& blocks) {
    size_t leaf_offset = Config::GetLeafOffset(tree_height_);

    for (size_t i = 0; i < blocks.size(); ++i) {
        size_t leaf_index = leaf_offset + i;
        if (leaf_index < num_nodes_) {
            tree_[leaf_index].AddBlock(blocks[i]);
        }
    }
}

void PathORAMServer::InitializeTreeWithDummies() {
    // Pad all buckets to full capacity with dummy blocks (ID = ~0)
    for (auto& bucket : tree_) {
        while (bucket.GetBlockCount() < Config::kBucketSize) {
            // Dummy block has max ID and random data
            std::vector<uint8_t> dummy_data(Config::kBlockSize);
            for (size_t i = 0; i < dummy_data.size(); ++i) {
                dummy_data[i] = rand() % 256;
            }
            core::Block dummy(~0ULL, dummy_data);
            bucket.AddBlock(dummy);
        }
    }
}

std::vector<uint8_t> PathORAMServer::EncryptBucket(const core::Bucket& bucket) {
    auto serialized = core::Serializer::SerializeBucket(bucket);
    auto iv = crypto::AES_CTR::GenerateIV();

    auto ciphertext = cipher_->Encrypt(serialized, iv);

    // Prepend IV to ciphertext
    std::vector<uint8_t> result;
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());

    return result;
}

core::Bucket PathORAMServer::DecryptBucket(const std::vector<uint8_t>& encrypted_data) {
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

void PathORAMServer::HandleReadBucket(size_t bucket_index) {
    if (bucket_index >= num_nodes_) {
        throw std::out_of_range("Invalid bucket index");
    }

    // Encrypt bucket
    auto encrypted = EncryptBucket(tree_[bucket_index]);

    // Send size then data
    uint64_t size = encrypted.size();
    net_io_->SendData(&size, sizeof(size));
    net_io_->SendData(encrypted.data(), encrypted.size());
    net_io_->Flush();
}

void PathORAMServer::HandleWriteBucket(size_t bucket_index) {
    if (bucket_index >= num_nodes_) {
        throw std::out_of_range("Invalid bucket index");
    }

    // Receive size
    uint64_t size;
    net_io_->RecvData(&size, sizeof(size));

    // Receive encrypted bucket
    std::vector<uint8_t> encrypted(size);
    net_io_->RecvData(encrypted.data(), size);

    // Decrypt and store
    tree_[bucket_index] = DecryptBucket(encrypted);
}

void PathORAMServer::HandleRequests() {
    running_ = true;
    std::cout << "Server ready to handle requests..." << std::endl;

    while (running_) {
        try {
            // Read command (1 byte: 'R' for read, 'W' for write, 'Q' for quit)
            char command;
            net_io_->RecvData(&command, 1);

            if (command == 'Q') {
                std::cout << "Server received quit command" << std::endl;
                break;
            }

            // Read bucket index
            uint64_t bucket_index;
            net_io_->RecvData(&bucket_index, sizeof(bucket_index));

            if (command == 'R') {
                HandleReadBucket(bucket_index);
            } else if (command == 'W') {
                HandleWriteBucket(bucket_index);
            } else {
                std::cerr << "Unknown command: " << command << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "Error handling request: " << e.what() << std::endl;
            break;
        }
    }

    running_ = false;
}

void PathORAMServer::Stop() { running_ = false; }

}  // namespace oram::path_oram
