#include "oram/ring_oram/RingORAMServer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace oram::ring_oram {

namespace {

void SendAck(network::NetIO* net_io) {
    char ack = 'K';
    net_io->SendData(&ack, sizeof(ack));
    net_io->Flush();
}

void XorInto(std::vector<uint8_t>* target, const std::vector<uint8_t>& source) {
    if (target == nullptr || target->size() != source.size()) {
        throw std::invalid_argument("Ring ORAM server XOR size mismatch");
    }
    for (size_t i = 0; i < source.size(); ++i) {
        (*target)[i] ^= source[i];
    }
}

RingBucketMetadata MetadataFromBucket(const EncryptedRingBucket& bucket) {
    RingBucketMetadata metadata;
    metadata.count = bucket.count;
    metadata.valids = bucket.valids;
    metadata.addrs = bucket.addrs;
    metadata.leaves = bucket.leaves;
    metadata.ptrs = bucket.ptrs;
    return metadata;
}

std::string SanitizePathPart(std::string value) {
    std::replace_if(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isalnum(ch) == 0 && ch != '-' && ch != '_'; }, '_');
    return value;
}

std::string DefaultServerStorageDir(const std::string& address, int port) {
    const auto dirname = "server_" + SanitizePathPart(address) + "_" + std::to_string(port);
    return (std::filesystem::temp_directory_path() / "unified_oramhub_ring_oram" / dirname)
        .string();
}

std::string ResolveServerStorageDir(const std::string& address, int port,
                                    const RuntimeConfig& config) {
    if (!config.server_storage_dir.empty()) {
        return config.server_storage_dir;
    }
    return DefaultServerStorageDir(address, port);
}

}  // namespace

RingORAMServer::RingORAMServer(const std::string& address, int port, const RuntimeConfig& config)
    : config_(config),
      storage_dir_(ResolveServerStorageDir(address, port, config)),
      tree_dir_((std::filesystem::path(storage_dir_) / "tree").string()) {
    ValidateConfig();
    std::filesystem::create_directories(tree_dir_);
    net_io_ = std::make_unique<network::NetIO>(address, port, true, true);
}

RingORAMServer::~RingORAMServer() { Stop(); }

void RingORAMServer::ValidateConfig() const {
    if (config_.num_blocks == 0) {
        throw std::invalid_argument("Ring ORAM requires at least one block");
    }
    if (config_.tree_depth >= 63) {
        throw std::invalid_argument("Ring ORAM tree depth is too large");
    }
    if (config_.z == 0) {
        throw std::invalid_argument("Ring ORAM bucket capacity Z must be positive");
    }
    if (config_.s == 0) {
        throw std::invalid_argument("Ring ORAM dummy slot count S must be positive");
    }
    if (config_.a == 0) {
        throw std::invalid_argument("Ring ORAM eviction rate A must be positive");
    }
    if (config_.block_size == 0) {
        throw std::invalid_argument("Ring ORAM block size must be positive");
    }
}

void RingORAMServer::ValidateInitialized() const {
    if (node_count_ != config_.NumTreeNodes() || !std::filesystem::is_directory(tree_dir_)) {
        throw std::runtime_error("Ring ORAM server has not been initialized");
    }
}

std::string RingORAMServer::BucketPath(size_t bucket_idx) const {
    return (std::filesystem::path(tree_dir_) / ("bucket_" + std::to_string(bucket_idx) + ".bin"))
        .string();
}

void RingORAMServer::ValidateBucketShape(const EncryptedRingBucket& bucket,
                                         const std::string& context) const {
    if (bucket.valids.size() != config_.BucketSlots() || bucket.addrs.size() != config_.z ||
        bucket.leaves.size() != config_.z || bucket.ptrs.size() != config_.z ||
        bucket.data.size() != config_.BucketSlots()) {
        throw std::invalid_argument("Ring ORAM " + context + " bucket shape mismatch");
    }
}

EncryptedRingBucket RingORAMServer::ReadBucketFromDisk(size_t bucket_idx) const {
    if (bucket_idx >= node_count_) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }

    std::ifstream input(BucketPath(bucket_idx), std::ios::binary);
    if (!input) {
        throw std::runtime_error("Ring ORAM bucket file is missing");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    EncryptedRingBucket bucket = DeserializeEncryptedRingBucket(bytes);
    ValidateBucketShape(bucket, "stored");
    return bucket;
}

void RingORAMServer::WriteBucketToDisk(size_t bucket_idx,
                                       const EncryptedRingBucket& bucket) const {
    if (bucket_idx >= node_count_) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }
    ValidateBucketShape(bucket, "stored");

    std::filesystem::create_directories(tree_dir_);
    const auto bytes = SerializeEncryptedRingBucket(bucket);
    std::ofstream output(BucketPath(bucket_idx), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Ring ORAM bucket file could not be opened for writing");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("Ring ORAM bucket file write failed");
    }
}

std::vector<size_t> RingORAMServer::GetPathIndices(uint64_t leaf) const {
    if (leaf >= config_.NumLeaves()) {
        throw std::out_of_range("Ring ORAM leaf index out of range");
    }

    std::vector<size_t> path;
    path.reserve(config_.tree_depth + 1);

    size_t node = 0;
    path.push_back(node);
    for (size_t level = 0; level < config_.tree_depth; ++level) {
        const size_t bit = (leaf >> (config_.tree_depth - 1 - level)) & 1ULL;
        node = 2 * node + 1 + bit;
        path.push_back(node);
    }
    return path;
}

size_t RingORAMServer::GetBucketIndex(uint64_t leaf, size_t level) const {
    if (leaf >= config_.NumLeaves()) {
        throw std::out_of_range("Ring ORAM leaf index out of range");
    }
    if (level > config_.tree_depth) {
        throw std::out_of_range("Ring ORAM level out of range");
    }

    size_t node = 0;
    for (size_t depth = 0; depth < level; ++depth) {
        const size_t bit = (leaf >> (config_.tree_depth - 1 - depth)) & 1ULL;
        node = 2 * node + 1 + bit;
    }
    return node;
}

size_t RingORAMServer::GetNodeLevel(size_t bucket_idx) const {
    if (bucket_idx >= config_.NumTreeNodes()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }

    size_t level = 0;
    while (bucket_idx > 0) {
        bucket_idx = (bucket_idx - 1) / 2;
        ++level;
    }
    return level;
}

bool RingORAMServer::IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const {
    const size_t level = GetNodeLevel(bucket_idx);
    return GetBucketIndex(leaf, level) == bucket_idx;
}

void RingORAMServer::HandleInit() {
    uint64_t node_count = 0;
    net_io_->RecvData(&node_count, sizeof(node_count));
    if (node_count != config_.NumTreeNodes()) {
        throw std::invalid_argument("Ring ORAM init tree size mismatch");
    }

    std::filesystem::remove_all(tree_dir_);
    std::filesystem::create_directories(tree_dir_);
    node_count_ = static_cast<size_t>(node_count);
    for (uint64_t i = 0; i < node_count; ++i) {
        std::vector<uint8_t> bytes;
        net_io_->RecvVec(bytes);
        EncryptedRingBucket bucket = DeserializeEncryptedRingBucket(bytes);
        ValidateBucketShape(bucket, "init");
        WriteBucketToDisk(static_cast<size_t>(i), bucket);
    }
    SendAck(net_io_.get());
}

void RingORAMServer::HandleReadPathMetadata() {
    ValidateInitialized();
    uint64_t leaf = 0;
    net_io_->RecvData(&leaf, sizeof(leaf));

    const auto path = GetPathIndices(leaf);
    uint64_t path_size = path.size();
    net_io_->SendData(&path_size, sizeof(path_size));
    for (size_t bucket_idx : path) {
        const EncryptedRingBucket bucket = ReadBucketFromDisk(bucket_idx);
        net_io_->SendVec(SerializeRingBucketMetadata(MetadataFromBucket(bucket)));
    }
    net_io_->Flush();
}

void RingORAMServer::HandleXorPathSlots() {
    ValidateInitialized();
    uint64_t leaf = 0;
    uint64_t offset_count = 0;
    net_io_->RecvData(&leaf, sizeof(leaf));
    net_io_->RecvData(&offset_count, sizeof(offset_count));

    const auto path = GetPathIndices(leaf);
    if (offset_count != path.size()) {
        throw std::invalid_argument("Ring ORAM XOR path offset count mismatch");
    }

    std::vector<size_t> offsets;
    offsets.reserve(offset_count);
    for (uint64_t i = 0; i < offset_count; ++i) {
        uint64_t offset = 0;
        net_io_->RecvData(&offset, sizeof(offset));
        offsets.push_back(static_cast<size_t>(offset));
    }

    std::vector<uint8_t> aggregate(config_.block_size, 0);
    std::vector<std::vector<uint8_t>> selected_ivs;
    selected_ivs.reserve(offsets.size());

    for (size_t i = 0; i < path.size(); ++i) {
        EncryptedRingBucket bucket = ReadBucketFromDisk(path[i]);
        const size_t offset = offsets[i];
        if (offset >= bucket.data.size() || offset >= bucket.valids.size()) {
            throw std::out_of_range("Ring ORAM XOR path slot out of range");
        }
        if (!bucket.valids[offset]) {
            throw std::runtime_error("Ring ORAM XOR path selected an invalid slot");
        }
        if (bucket.data[offset].ciphertext.size() != config_.block_size) {
            throw std::runtime_error("Ring ORAM XOR path block size mismatch");
        }

        XorInto(&aggregate, bucket.data[offset].ciphertext);
        selected_ivs.push_back(bucket.data[offset].iv);
        bucket.valids[offset] = false;
        ++bucket.count;
        WriteBucketToDisk(path[i], bucket);
    }

    ++xor_path_read_count_;
    last_xor_path_slot_count_ = offsets.size();

    net_io_->SendVec(aggregate);
    uint64_t iv_count = selected_ivs.size();
    net_io_->SendData(&iv_count, sizeof(iv_count));
    for (const auto& iv : selected_ivs) {
        net_io_->SendVec(iv);
    }
    net_io_->Flush();
}

void RingORAMServer::HandleReadBucket() {
    ValidateInitialized();
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    net_io_->SendVec(SerializeEncryptedRingBucket(ReadBucketFromDisk(bucket_idx)));
    net_io_->Flush();
}

void RingORAMServer::HandleWriteBucket() {
    ValidateInitialized();
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));

    std::vector<uint8_t> bytes;
    net_io_->RecvVec(bytes);
    EncryptedRingBucket bucket = DeserializeEncryptedRingBucket(bytes);
    ValidateBucketShape(bucket, "write");
    WriteBucketToDisk(static_cast<size_t>(bucket_idx), bucket);
    SendAck(net_io_.get());
}

void RingORAMServer::HandleReadBucketCount() {
    ValidateInitialized();
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    uint64_t count = ReadBucketFromDisk(bucket_idx).count;
    net_io_->SendData(&count, sizeof(count));
    net_io_->Flush();
}

void RingORAMServer::HandleReadStats() {
    net_io_->SendData(&xor_path_read_count_, sizeof(xor_path_read_count_));
    uint64_t last_slots = last_xor_path_slot_count_;
    net_io_->SendData(&last_slots, sizeof(last_slots));
    net_io_->Flush();
}

void RingORAMServer::HandleRequests() {
    running_ = true;
    while (running_) {
        try {
            char command = 0;
            net_io_->RecvData(&command, 1);
            if (command == 'Q') {
                break;
            }
            if (command == 'I') {
                HandleInit();
            } else if (command == 'M') {
                HandleReadPathMetadata();
            } else if (command == 'X') {
                HandleXorPathSlots();
            } else if (command == 'B') {
                HandleReadBucket();
            } else if (command == 'W') {
                HandleWriteBucket();
            } else if (command == 'T') {
                HandleReadBucketCount();
            } else if (command == 'S') {
                HandleReadStats();
            } else {
                throw std::runtime_error("Unknown Ring ORAM server command");
            }
        } catch (...) {
            break;
        }
    }
    running_ = false;
}

void RingORAMServer::Stop() { running_ = false; }

}  // namespace oram::ring_oram
