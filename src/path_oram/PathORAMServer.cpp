#include "oram/path_oram/PathORAMServer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include "oram/core/Serializer.h"

namespace oram::path_oram {

namespace {

std::string SanitizePathPart(std::string value) {
    std::replace_if(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isalnum(ch) == 0 && ch != '-' && ch != '_'; }, '_');
    return value;
}

std::string DefaultServerStorageDir(const std::string& address, int port) {
    const auto dirname = "server_" + SanitizePathPart(address) + "_" + std::to_string(port);
    return (std::filesystem::temp_directory_path() / "unified_oramhub_path_oram" / dirname)
        .string();
}

std::string ResolveServerStorageDir(const std::string& address, int port,
                                    const std::string& storage_dir) {
    if (!storage_dir.empty()) {
        return storage_dir;
    }
    return DefaultServerStorageDir(address, port);
}

}  // namespace

PathORAMServer::PathORAMServer(const std::string& address, int port, size_t tree_height,
                               const std::string& storage_dir)
    : tree_height_(tree_height),
      num_nodes_(Config::GetNumTreeNodes(tree_height)),
      num_leaves_(Config::GetNumLeaves(tree_height)),
      node_count_(0),
      storage_dir_(ResolveServerStorageDir(address, port, storage_dir)),
      tree_dir_((std::filesystem::path(storage_dir_) / "tree").string()),
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

PathORAMServer::RequestStats PathORAMServer::GetRequestStats() const { return request_stats_; }

void PathORAMServer::Init(const std::vector<core::Block>& initial_blocks) {
    if (initial_blocks.size() > num_leaves_) {
        throw std::invalid_argument("Too many blocks for tree capacity");
    }

    std::filesystem::remove_all(tree_dir_);
    std::filesystem::create_directories(tree_dir_);
    node_count_ = num_nodes_;

    const size_t leaf_offset = Config::GetLeafOffset(tree_height_);
    for (size_t bucket_index = 0; bucket_index < num_nodes_; ++bucket_index) {
        core::Bucket bucket(Config::kBucketSize);

        if (bucket_index >= leaf_offset) {
            const size_t leaf = bucket_index - leaf_offset;
            if (leaf < initial_blocks.size()) {
                bucket.AddBlock(initial_blocks[leaf]);
            }
        }

        FillBucketWithDummies(bucket);
        WriteBucketToDisk(bucket_index, bucket);
    }

    std::cout << "Server initialized with " << initial_blocks.size() << " real blocks" << std::endl;
}

void PathORAMServer::FillBucketWithDummies(core::Bucket& bucket) {
    // Pad the bucket to full capacity with dummy blocks (ID = ~0).
    while (bucket.GetBlockCount() < Config::kBucketSize) {
        std::vector<uint8_t> dummy_data(Config::kBlockSize);
        for (size_t i = 0; i < dummy_data.size(); ++i) {
            dummy_data[i] = static_cast<uint8_t>(std::rand() % 256);
        }
        core::Block dummy(~0ULL, dummy_data);
        bucket.AddBlock(dummy);
    }
}

void PathORAMServer::ValidateInitialized() const {
    if (node_count_ != num_nodes_ || !std::filesystem::is_directory(tree_dir_)) {
        throw std::runtime_error("Path ORAM server has not been initialized");
    }
}

void PathORAMServer::ValidateBucketShape(const core::Bucket& bucket,
                                         const std::string& context) const {
    if (bucket.GetCapacity() != Config::kBucketSize ||
        bucket.GetBlockCount() > Config::kBucketSize) {
        throw std::invalid_argument("Path ORAM " + context + " bucket shape mismatch");
    }
}

std::string PathORAMServer::BucketPath(size_t bucket_index) const {
    return (std::filesystem::path(tree_dir_) / ("bucket_" + std::to_string(bucket_index) + ".bin"))
        .string();
}

core::Bucket PathORAMServer::ReadBucketFromDisk(size_t bucket_index) const {
    if (bucket_index >= node_count_) {
        throw std::out_of_range("Invalid bucket index");
    }

    std::ifstream input(BucketPath(bucket_index), std::ios::binary);
    if (!input) {
        throw std::runtime_error("Path ORAM bucket file is missing");
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(uint64_t)) {
        throw std::runtime_error("Path ORAM bucket file is malformed");
    }

    core::Bucket bucket = core::Serializer::DeserializeBucket(bytes, Config::kBucketSize);
    ValidateBucketShape(bucket, "stored");
    return bucket;
}

void PathORAMServer::WriteBucketToDisk(size_t bucket_index, const core::Bucket& bucket) const {
    if (bucket_index >= node_count_) {
        throw std::out_of_range("Invalid bucket index");
    }
    ValidateBucketShape(bucket, "stored");

    std::filesystem::create_directories(tree_dir_);
    const auto bytes = core::Serializer::SerializeBucket(bucket);
    std::ofstream output(BucketPath(bucket_index), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Path ORAM bucket file could not be opened for writing");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("Path ORAM bucket file write failed");
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
    ValidateInitialized();
    ++request_stats_.read_bucket_requests;

    // Encrypt bucket
    auto encrypted = EncryptBucket(ReadBucketFromDisk(bucket_index));

    // Send size then data
    uint64_t size = encrypted.size();
    net_io_->SendData(&size, sizeof(size));
    net_io_->SendData(encrypted.data(), encrypted.size());
    net_io_->Flush();
}

void PathORAMServer::HandleWriteBucket(size_t bucket_index) {
    ValidateInitialized();
    ++request_stats_.write_bucket_requests;

    // Receive size
    uint64_t size;
    net_io_->RecvData(&size, sizeof(size));

    // Receive encrypted bucket
    std::vector<uint8_t> encrypted(size);
    net_io_->RecvData(encrypted.data(), size);

    // Decrypt and store
    WriteBucketToDisk(bucket_index, DecryptBucket(encrypted));
}

void PathORAMServer::HandleReadPath() {
    ValidateInitialized();

    uint64_t count = 0;
    net_io_->RecvData(&count, sizeof(count));
    if (count != tree_height_ + 1) {
        throw std::invalid_argument("Path ORAM read path request count mismatch");
    }

    std::vector<uint64_t> bucket_indices(count);
    for (uint64_t& bucket_index : bucket_indices) {
        net_io_->RecvData(&bucket_index, sizeof(bucket_index));
    }

    ++request_stats_.read_path_requests;

    net_io_->SendData(&count, sizeof(count));
    for (uint64_t bucket_index : bucket_indices) {
        auto encrypted = EncryptBucket(ReadBucketFromDisk(static_cast<size_t>(bucket_index)));
        uint64_t size = encrypted.size();
        net_io_->SendData(&size, sizeof(size));
        if (!encrypted.empty()) {
            net_io_->SendData(encrypted.data(), encrypted.size());
        }
    }
    net_io_->Flush();
}

void PathORAMServer::HandleWritePath() {
    ValidateInitialized();

    uint64_t count = 0;
    net_io_->RecvData(&count, sizeof(count));
    if (count != tree_height_ + 1) {
        throw std::invalid_argument("Path ORAM write path request count mismatch");
    }

    for (uint64_t i = 0; i < count; ++i) {
        uint64_t bucket_index = 0;
        net_io_->RecvData(&bucket_index, sizeof(bucket_index));

        uint64_t size = 0;
        net_io_->RecvData(&size, sizeof(size));

        std::vector<uint8_t> encrypted(size);
        if (size > 0) {
            net_io_->RecvData(encrypted.data(), encrypted.size());
        }

        WriteBucketToDisk(static_cast<size_t>(bucket_index), DecryptBucket(encrypted));
    }

    ++request_stats_.write_path_requests;

    char ack = 'A';
    net_io_->SendData(&ack, 1);
    net_io_->Flush();
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

            if (command == 'R') {
                uint64_t bucket_index;
                net_io_->RecvData(&bucket_index, sizeof(bucket_index));
                HandleReadBucket(bucket_index);
            } else if (command == 'W') {
                uint64_t bucket_index;
                net_io_->RecvData(&bucket_index, sizeof(bucket_index));
                HandleWriteBucket(bucket_index);
            } else if (command == 'P') {
                HandleReadPath();
            } else if (command == 'T') {
                HandleWritePath();
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
