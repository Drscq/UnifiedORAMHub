#include "oram/ring_oram/RingORAMServer.h"

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

}  // namespace

RingORAMServer::RingORAMServer(const std::string& address, int port, const RuntimeConfig& config)
    : config_(config) {
    ValidateConfig();
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
    if (tree_.size() != config_.NumTreeNodes()) {
        throw std::runtime_error("Ring ORAM server has not been initialized");
    }
}

EncryptedRingBucket& RingORAMServer::GetBucket(size_t bucket_idx) {
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }
    return tree_[bucket_idx];
}

const EncryptedRingBucket& RingORAMServer::GetBucket(size_t bucket_idx) const {
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }
    return tree_[bucket_idx];
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
    if (bucket_idx >= tree_.size()) {
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

    tree_.clear();
    tree_.reserve(node_count);
    for (uint64_t i = 0; i < node_count; ++i) {
        std::vector<uint8_t> bytes;
        net_io_->RecvVec(bytes);
        EncryptedRingBucket bucket = DeserializeEncryptedRingBucket(bytes);
        if (bucket.valids.size() != config_.BucketSlots() || bucket.addrs.size() != config_.z ||
            bucket.leaves.size() != config_.z || bucket.ptrs.size() != config_.z ||
            bucket.data.size() != config_.BucketSlots()) {
            throw std::invalid_argument("Ring ORAM init bucket shape mismatch");
        }
        tree_.push_back(std::move(bucket));
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
        net_io_->SendVec(SerializeRingBucketMetadata(MetadataFromBucket(GetBucket(bucket_idx))));
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
        EncryptedRingBucket& bucket = GetBucket(path[i]);
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
    net_io_->SendVec(SerializeEncryptedRingBucket(GetBucket(bucket_idx)));
    net_io_->Flush();
}

void RingORAMServer::HandleWriteBucket() {
    ValidateInitialized();
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));

    std::vector<uint8_t> bytes;
    net_io_->RecvVec(bytes);
    EncryptedRingBucket bucket = DeserializeEncryptedRingBucket(bytes);
    if (bucket.valids.size() != config_.BucketSlots() || bucket.addrs.size() != config_.z ||
        bucket.leaves.size() != config_.z || bucket.ptrs.size() != config_.z ||
        bucket.data.size() != config_.BucketSlots()) {
        throw std::invalid_argument("Ring ORAM write bucket shape mismatch");
    }
    GetBucket(bucket_idx) = std::move(bucket);
    SendAck(net_io_.get());
}

void RingORAMServer::HandleReadBucketCount() {
    ValidateInitialized();
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    uint64_t count = GetBucket(bucket_idx).count;
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
