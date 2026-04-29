#include "oram/ring_oram/RingORAMClient.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace oram::ring_oram {

namespace {

std::vector<uint8_t> Uint64ToBytes(uint64_t value) {
    std::vector<uint8_t> bytes(sizeof(value), 0);
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

uint64_t BytesToUint64(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != sizeof(uint64_t)) {
        throw std::invalid_argument("Ring ORAM uint64 field has invalid size");
    }
    uint64_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

std::vector<uint8_t> FixedClientKey() {
    // Test/demo key. A production deployment must provision this out of band.
    return std::vector<uint8_t>(crypto::AES_CTR::kKeySize128, 0x52);
}

}  // namespace

RingORAMClient::RingORAMClient(const std::string& server_address, int port,
                               const RuntimeConfig& config, uint64_t seed)
    : config_(config),
      prng_(seed),
      net_io_(std::make_unique<network::NetIO>(server_address, port, false, true)),
      cipher_(std::make_unique<crypto::AES_CTR>(FixedClientKey())) {
    if (config_.num_blocks == 0 || config_.z == 0 || config_.s == 0 || config_.a == 0 ||
        config_.block_size == 0 || config_.tree_depth >= 63) {
        throw std::invalid_argument("Invalid Ring ORAM client configuration");
    }

    for (uint64_t addr = 0; addr < config_.num_blocks; ++addr) {
        position_map_.emplace(addr, GetRandomLeaf());
        stash_.push_back(RingBlock{addr, position_map_.at(addr),
                                   std::vector<uint8_t>(config_.block_size, 0)});
    }

    InitializeServerStorage();
}

RingORAMClient::~RingORAMClient() {
    try {
        char quit = 'Q';
        net_io_->SendData(&quit, 1);
        net_io_->Flush();
    } catch (...) {
    }
}

uint64_t RingORAMClient::GetRandomLeaf() {
    std::uniform_int_distribution<uint64_t> dist(0, config_.NumLeaves() - 1);
    return dist(prng_);
}

void RingORAMClient::ValidateAddress(uint64_t addr) const {
    if (addr >= config_.num_blocks) {
        throw std::out_of_range("Ring ORAM address out of range");
    }
}

size_t RingORAMClient::GetBucketIndex(uint64_t leaf, size_t level) const {
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

size_t RingORAMClient::GetNodeLevel(size_t bucket_idx) const {
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

bool RingORAMClient::IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const {
    const size_t level = GetNodeLevel(bucket_idx);
    return GetBucketIndex(leaf, level) == bucket_idx;
}

std::vector<size_t> RingORAMClient::GetPathIndices(uint64_t leaf) const {
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

std::vector<uint8_t> RingORAMClient::Access(core::Op op, uint64_t addr,
                                            const std::vector<uint8_t>& data) {
    ValidateAddress(addr);
    if (op == core::Op::WRITE && data.size() != config_.block_size) {
        throw std::invalid_argument("Ring ORAM write size does not match block size");
    }

    const uint64_t old_leaf = position_map_.at(addr);
    const uint64_t new_leaf = GetRandomLeaf();
    position_map_[addr] = new_leaf;

    const auto path_data = ReadPath(old_leaf, addr);
    RingBlock block;
    if (path_data.has_value()) {
        block = RingBlock{addr, new_leaf, *path_data};
        RemoveBlockFromStash(addr);
    } else {
        auto stashed = TakeBlockFromStash(addr);
        if (stashed.has_value()) {
            block = *stashed;
            block.leaf = new_leaf;
        } else {
            block = RingBlock{addr, new_leaf, std::vector<uint8_t>(config_.block_size, 0)};
        }
    }

    if (op == core::Op::WRITE) {
        block.data = data;
    }
    block.addr = addr;
    block.leaf = new_leaf;
    PutBlockInStash(block);

    round_ = (round_ + 1) % config_.a;
    if (round_ == 0) {
        EvictPath();
    }
    EarlyReshuffle(old_leaf);

    return block.data;
}

std::vector<uint8_t> RingORAMClient::Read(uint64_t addr) { return Access(core::Op::READ, addr, {}); }

void RingORAMClient::Write(uint64_t addr, const std::vector<uint8_t>& data) {
    Access(core::Op::WRITE, addr, data);
}

std::optional<std::vector<uint8_t>> RingORAMClient::ReadPath(uint64_t leaf, uint64_t addr) {
    const auto metadata = FetchPathMetadata(leaf);
    if (metadata.size() != config_.tree_depth + 1) {
        throw std::runtime_error("Ring ORAM path metadata length mismatch");
    }

    std::vector<size_t> offsets;
    offsets.reserve(metadata.size());
    std::vector<bool> target_flags;
    target_flags.reserve(metadata.size());

    for (const auto& encrypted_metadata : metadata) {
        RingBucket bucket = DecryptMetadata(encrypted_metadata);
        const size_t offset = GetBlockOffset(bucket, addr);
        if (offset >= bucket.valids.size()) {
            throw std::out_of_range("Ring ORAM selected slot out of range");
        }

        bool selected_target = false;
        for (size_t i = 0; i < bucket.addrs.size(); ++i) {
            if (bucket.addrs[i] == addr && bucket.ptrs[i] == offset && bucket.valids[offset]) {
                selected_target = true;
                break;
            }
        }
        offsets.push_back(offset);
        target_flags.push_back(selected_target);
    }

    ++read_path_count_;
    return FetchXorPath(leaf, offsets, target_flags);
}

void RingORAMClient::EvictPath() {
    const uint64_t leaf = eviction_counter_ % config_.NumLeaves();
    ++eviction_counter_;

    const auto path = GetPathIndices(leaf);
    for (size_t bucket_idx : path) {
        ReadBucket(bucket_idx);
    }
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        WriteBucket(*it);
    }
}

void RingORAMClient::EarlyReshuffle(uint64_t leaf) {
    const auto path = GetPathIndices(leaf);
    for (size_t bucket_idx : path) {
        if (FetchServerBucketCount(bucket_idx) >= config_.s) {
            ReadBucket(bucket_idx);
            WriteBucket(bucket_idx);
            ++early_reshuffle_count_;
        }
    }
}

size_t RingORAMClient::GetBlockOffset(const RingBucket& bucket, uint64_t addr) {
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] == addr && offset < bucket.valids.size() && bucket.valids[offset]) {
            return offset;
        }
    }

    const auto dummies = ValidDummyOffsets(bucket);
    if (dummies.empty()) {
        throw std::runtime_error("Ring ORAM bucket has no valid dummy slot");
    }

    std::uniform_int_distribution<size_t> dist(0, dummies.size() - 1);
    return dummies[dist(prng_)];
}

void RingORAMClient::ReadBucket(size_t bucket_idx) {
    RingBucket bucket = DecryptBucket(ReadEncryptedBucketFromServer(bucket_idx));

    size_t reads = 0;
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] == kDummyAddress || offset >= bucket.valids.size() ||
            !bucket.valids[offset]) {
            continue;
        }

        RingBlock block = bucket.data[offset];
        block.addr = bucket.addrs[i];
        block.leaf = bucket.leaves[i];
        block.data.resize(config_.block_size, 0);
        PutBlockInStash(block);
        bucket.valids[offset] = false;
        ++reads;
    }

    while (reads < config_.z) {
        const auto dummies = ValidDummyOffsets(bucket);
        if (dummies.empty()) {
            throw std::runtime_error("Ring ORAM bucket lacks dummy padding for a full read");
        }
        std::uniform_int_distribution<size_t> dist(0, dummies.size() - 1);
        bucket.valids[dummies[dist(prng_)]] = false;
        ++reads;
    }
}

void RingORAMClient::WriteBucket(size_t bucket_idx) {
    RingBucket bucket(config_.z, config_.s, config_.block_size);
    FillBucketFromStash(bucket_idx, &bucket);
    WriteEncryptedBucketToServer(bucket_idx, EncryptBucket(bucket));
}

bool RingORAMClient::CanResideInBucket(uint64_t block_leaf, size_t bucket_idx) const {
    return IsBucketOnLeafPath(bucket_idx, block_leaf);
}

std::vector<uint8_t> RingORAMClient::XorBuffers(const std::vector<uint8_t>& lhs,
                                                const std::vector<uint8_t>& rhs) const {
    if (lhs.size() != rhs.size()) {
        throw std::invalid_argument("Ring ORAM XOR buffer size mismatch");
    }

    std::vector<uint8_t> result(lhs.size(), 0);
    for (size_t i = 0; i < lhs.size(); ++i) {
        result[i] = lhs[i] ^ rhs[i];
    }
    return result;
}

void RingORAMClient::XorInto(std::vector<uint8_t>* target,
                             const std::vector<uint8_t>& source) const {
    if (target == nullptr || target->size() != source.size()) {
        throw std::invalid_argument("Ring ORAM XOR target size mismatch");
    }

    for (size_t i = 0; i < source.size(); ++i) {
        (*target)[i] ^= source[i];
    }
}

void RingORAMClient::PutBlockInStash(const RingBlock& block) {
    if (block.IsDummy()) {
        return;
    }
    RemoveBlockFromStash(block.addr);
    stash_.push_back(block);
}

std::optional<RingBlock> RingORAMClient::TakeBlockFromStash(uint64_t addr) {
    for (auto it = stash_.begin(); it != stash_.end(); ++it) {
        if (it->addr == addr) {
            RingBlock block = *it;
            stash_.erase(it);
            return block;
        }
    }
    return std::nullopt;
}

void RingORAMClient::RemoveBlockFromStash(uint64_t addr) {
    stash_.erase(std::remove_if(stash_.begin(), stash_.end(),
                                [addr](const RingBlock& block) { return block.addr == addr; }),
                 stash_.end());
}

std::vector<size_t> RingORAMClient::ValidRealOffsets(const RingBucket& bucket) const {
    std::vector<size_t> offsets;
    offsets.reserve(bucket.Z());
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] != kDummyAddress && offset < bucket.valids.size() &&
            bucket.valids[offset]) {
            offsets.push_back(offset);
        }
    }
    return offsets;
}

std::vector<size_t> RingORAMClient::ValidDummyOffsets(const RingBucket& bucket) const {
    std::vector<bool> is_real(bucket.valids.size(), false);
    for (size_t offset : ValidRealOffsets(bucket)) {
        is_real[offset] = true;
    }

    std::vector<size_t> offsets;
    offsets.reserve(bucket.valids.size());
    for (size_t offset = 0; offset < bucket.valids.size(); ++offset) {
        if (bucket.valids[offset] && !is_real[offset]) {
            offsets.push_back(offset);
        }
    }
    return offsets;
}

void RingORAMClient::InitializeServerStorage() {
    const auto tree = BuildInitialPlainTree();

    char command = 'I';
    net_io_->SendData(&command, 1);
    uint64_t node_count = tree.size();
    net_io_->SendData(&node_count, sizeof(node_count));
    for (const auto& bucket : tree) {
        net_io_->SendVec(SerializeEncryptedRingBucket(EncryptBucket(bucket)));
    }
    net_io_->Flush();
    ExpectAck();
}

std::vector<RingBucket> RingORAMClient::BuildInitialPlainTree() {
    std::vector<RingBucket> tree;
    tree.reserve(config_.NumTreeNodes());
    for (size_t i = 0; i < config_.NumTreeNodes(); ++i) {
        tree.emplace_back(config_.z, config_.s, config_.block_size);
    }

    for (size_t idx = tree.size(); idx > 0; --idx) {
        FillBucketFromStash(idx - 1, &tree[idx - 1]);
    }
    return tree;
}

void RingORAMClient::FillBucketFromStash(size_t bucket_idx, RingBucket* bucket) {
    if (bucket == nullptr) {
        throw std::invalid_argument("Ring ORAM bucket pointer is null");
    }

    std::vector<RingBlock> selected;
    selected.reserve(config_.z);

    auto it = stash_.begin();
    while (it != stash_.end() && selected.size() < config_.z) {
        if (!it->IsDummy() && CanResideInBucket(it->leaf, bucket_idx)) {
            selected.push_back(*it);
            it = stash_.erase(it);
        } else {
            ++it;
        }
    }
    bucket->ResetWithBlocks(selected, &prng_);
}

EncryptedField RingORAMClient::EncryptBytes(const std::vector<uint8_t>& plaintext) {
    EncryptedField field;
    field.iv = crypto::AES_CTR::GenerateIV();
    field.ciphertext = cipher_->Encrypt(plaintext, field.iv);
    return field;
}

std::vector<uint8_t> RingORAMClient::DecryptBytes(const EncryptedField& field) {
    return cipher_->Decrypt(field.ciphertext, field.iv);
}

EncryptedField RingORAMClient::EncryptUint64(uint64_t value) {
    return EncryptBytes(Uint64ToBytes(value));
}

uint64_t RingORAMClient::DecryptUint64(const EncryptedField& field) {
    return BytesToUint64(DecryptBytes(field));
}

EncryptedRingBucket RingORAMClient::EncryptBucket(const RingBucket& bucket) {
    EncryptedRingBucket encrypted;
    encrypted.count = bucket.count;
    encrypted.valids = bucket.valids;
    encrypted.addrs.reserve(bucket.addrs.size());
    encrypted.leaves.reserve(bucket.leaves.size());
    encrypted.ptrs.reserve(bucket.ptrs.size());
    encrypted.data.reserve(bucket.data.size());

    for (uint64_t addr : bucket.addrs) {
        encrypted.addrs.push_back(EncryptUint64(addr));
    }
    for (uint64_t leaf : bucket.leaves) {
        encrypted.leaves.push_back(EncryptUint64(leaf));
    }
    for (size_t ptr : bucket.ptrs) {
        encrypted.ptrs.push_back(EncryptUint64(ptr));
    }
    for (const auto& block : bucket.data) {
        std::vector<uint8_t> payload = block.data;
        payload.resize(config_.block_size, 0);
        encrypted.data.push_back(EncryptBytes(payload));
    }
    return encrypted;
}

RingBucket RingORAMClient::DecryptBucket(const EncryptedRingBucket& encrypted) {
    if (encrypted.valids.size() != config_.BucketSlots() || encrypted.addrs.size() != config_.z ||
        encrypted.leaves.size() != config_.z || encrypted.ptrs.size() != config_.z ||
        encrypted.data.size() != config_.BucketSlots()) {
        throw std::invalid_argument("Ring ORAM encrypted bucket shape mismatch");
    }

    RingBucket bucket(config_.z, config_.s, config_.block_size);
    bucket.count = encrypted.count;
    bucket.valids = encrypted.valids;
    for (size_t i = 0; i < config_.z; ++i) {
        bucket.addrs[i] = DecryptUint64(encrypted.addrs[i]);
        bucket.leaves[i] = DecryptUint64(encrypted.leaves[i]);
        bucket.ptrs[i] = static_cast<size_t>(DecryptUint64(encrypted.ptrs[i]));
    }
    for (size_t offset = 0; offset < config_.BucketSlots(); ++offset) {
        bucket.data[offset] = RingBlock{kDummyAddress, 0, DecryptBytes(encrypted.data[offset])};
        bucket.data[offset].data.resize(config_.block_size, 0);
    }
    for (size_t i = 0; i < config_.z; ++i) {
        if (bucket.addrs[i] == kDummyAddress || bucket.ptrs[i] >= bucket.data.size()) {
            continue;
        }
        const size_t offset = bucket.ptrs[i];
        bucket.data[offset].addr = bucket.addrs[i];
        bucket.data[offset].leaf = bucket.leaves[i];
    }
    return bucket;
}

RingBucket RingORAMClient::DecryptMetadata(const RingBucketMetadata& metadata) {
    if (metadata.valids.size() != config_.BucketSlots() || metadata.addrs.size() != config_.z ||
        metadata.leaves.size() != config_.z || metadata.ptrs.size() != config_.z) {
        throw std::invalid_argument("Ring ORAM encrypted metadata shape mismatch");
    }

    RingBucket bucket(config_.z, config_.s, config_.block_size);
    bucket.count = metadata.count;
    bucket.valids = metadata.valids;
    for (size_t i = 0; i < config_.z; ++i) {
        bucket.addrs[i] = DecryptUint64(metadata.addrs[i]);
        bucket.leaves[i] = DecryptUint64(metadata.leaves[i]);
        bucket.ptrs[i] = static_cast<size_t>(DecryptUint64(metadata.ptrs[i]));
    }
    return bucket;
}

std::vector<RingBucketMetadata> RingORAMClient::FetchPathMetadata(uint64_t leaf) {
    char command = 'M';
    net_io_->SendData(&command, 1);
    net_io_->SendData(&leaf, sizeof(leaf));
    net_io_->Flush();

    uint64_t path_size = 0;
    net_io_->RecvData(&path_size, sizeof(path_size));
    std::vector<RingBucketMetadata> metadata;
    metadata.reserve(path_size);
    for (uint64_t i = 0; i < path_size; ++i) {
        std::vector<uint8_t> bytes;
        net_io_->RecvVec(bytes);
        metadata.push_back(DeserializeRingBucketMetadata(bytes));
    }
    return metadata;
}

std::optional<std::vector<uint8_t>> RingORAMClient::FetchXorPath(
    uint64_t leaf, const std::vector<size_t>& offsets, const std::vector<bool>& target_flags) {
    if (offsets.size() != target_flags.size()) {
        throw std::invalid_argument("Ring ORAM XOR target flag size mismatch");
    }

    char command = 'X';
    net_io_->SendData(&command, 1);
    net_io_->SendData(&leaf, sizeof(leaf));
    uint64_t offset_count = offsets.size();
    net_io_->SendData(&offset_count, sizeof(offset_count));
    for (size_t offset : offsets) {
        uint64_t offset_u64 = offset;
        net_io_->SendData(&offset_u64, sizeof(offset_u64));
    }
    net_io_->Flush();

    std::vector<uint8_t> aggregate_ciphertext;
    net_io_->RecvVec(aggregate_ciphertext);
    if (aggregate_ciphertext.size() != config_.block_size) {
        throw std::runtime_error("Ring ORAM XOR aggregate has invalid size");
    }

    uint64_t iv_count = 0;
    net_io_->RecvData(&iv_count, sizeof(iv_count));
    if (iv_count != offsets.size()) {
        throw std::runtime_error("Ring ORAM XOR IV count mismatch");
    }

    std::vector<std::vector<uint8_t>> selected_ivs;
    selected_ivs.reserve(iv_count);
    for (uint64_t i = 0; i < iv_count; ++i) {
        std::vector<uint8_t> iv;
        net_io_->RecvVec(iv);
        selected_ivs.push_back(std::move(iv));
    }

    bool found = false;
    std::vector<uint8_t> target_iv;
    std::vector<uint8_t> target_ciphertext = aggregate_ciphertext;
    const std::vector<uint8_t> zero_plaintext(config_.block_size, 0);

    for (size_t i = 0; i < target_flags.size(); ++i) {
        if (target_flags[i]) {
            if (found) {
                throw std::runtime_error("Ring ORAM read path selected multiple target slots");
            }
            found = true;
            target_iv = selected_ivs[i];
        } else {
            const auto dummy_ciphertext = cipher_->Encrypt(zero_plaintext, selected_ivs[i]);
            XorInto(&target_ciphertext, dummy_ciphertext);
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return cipher_->Decrypt(target_ciphertext, target_iv);
}

EncryptedRingBucket RingORAMClient::ReadEncryptedBucketFromServer(size_t bucket_idx) {
    char command = 'B';
    uint64_t idx = bucket_idx;
    net_io_->SendData(&command, 1);
    net_io_->SendData(&idx, sizeof(idx));
    net_io_->Flush();

    std::vector<uint8_t> bytes;
    net_io_->RecvVec(bytes);
    return DeserializeEncryptedRingBucket(bytes);
}

void RingORAMClient::WriteEncryptedBucketToServer(size_t bucket_idx,
                                                  const EncryptedRingBucket& bucket) {
    char command = 'W';
    uint64_t idx = bucket_idx;
    net_io_->SendData(&command, 1);
    net_io_->SendData(&idx, sizeof(idx));
    net_io_->SendVec(SerializeEncryptedRingBucket(bucket));
    net_io_->Flush();
    ExpectAck();
}

RingORAMClient::ServerStats RingORAMClient::FetchServerStats() {
    char command = 'S';
    net_io_->SendData(&command, 1);
    net_io_->Flush();

    ServerStats stats;
    uint64_t last_slots = 0;
    net_io_->RecvData(&stats.xor_path_read_count, sizeof(stats.xor_path_read_count));
    net_io_->RecvData(&last_slots, sizeof(last_slots));
    stats.last_xor_path_slot_count = static_cast<size_t>(last_slots);
    return stats;
}

uint64_t RingORAMClient::FetchServerBucketCount(size_t bucket_idx) {
    char command = 'T';
    uint64_t idx = bucket_idx;
    net_io_->SendData(&command, 1);
    net_io_->SendData(&idx, sizeof(idx));
    net_io_->Flush();

    uint64_t count = 0;
    net_io_->RecvData(&count, sizeof(count));
    return count;
}

uint64_t RingORAMClient::ServerBucketCount(size_t bucket_idx) {
    return FetchServerBucketCount(bucket_idx);
}

uint64_t RingORAMClient::ServerXorPathReadCount() { return FetchServerStats().xor_path_read_count; }

size_t RingORAMClient::ServerLastXorPathSlotCount() {
    return FetchServerStats().last_xor_path_slot_count;
}

void RingORAMClient::ExpectAck() {
    char ack = 0;
    net_io_->RecvData(&ack, sizeof(ack));
    if (ack != 'K') {
        throw std::runtime_error("Unexpected Ring ORAM server acknowledgment");
    }
}

}  // namespace oram::ring_oram
