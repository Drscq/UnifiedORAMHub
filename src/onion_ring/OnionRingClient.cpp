#include "oram/onion_ring/OnionRingClient.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>

namespace oram::onion_ring {

namespace {

std::vector<size_t> MakeRandomPermutation(size_t size, std::mt19937_64* prng) {
    std::vector<size_t> permutation(size);
    std::iota(permutation.begin(), permutation.end(), 0);
    std::shuffle(permutation.begin(), permutation.end(), *prng);
    return permutation;
}

std::vector<int64_t> ApplyPermutationToIds(const std::vector<int64_t>& ids,
                                           const std::vector<size_t>& permutation) {
    if (ids.size() != permutation.size()) {
        throw std::invalid_argument("ID vector and permutation size mismatch");
    }

    std::vector<int64_t> result(ids.size(), -1);
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        if (permutation[dest] >= ids.size()) {
            throw std::out_of_range("Permutation index out of range");
        }
        result[dest] = ids[permutation[dest]];
    }
    return result;
}

RLWECiphertext CloneCiphertext(const RLWECiphertext& source, const TLweParams* params) {
    RLWECiphertext copy(params);
    tLweCopy(copy.Get(), source.Get(), params);
    return copy;
}

RLWECiphertext ZeroCiphertext(const TLweParams* params) {
    RLWECiphertext zero(params);
    tLweClear(zero.Get(), params);
    return zero;
}

}  // namespace

OnionRingClient::OnionRingClient(const std::string& server_address, int port,
                                 const RuntimeConfig& config)
    : config_(config),
      num_nodes_(config_.NumTreeNodes()),
      num_leaves_(config_.NumLeaves()),
      pos_map_(config_.num_blocks, 0),
      id_map_(num_nodes_, std::vector<int64_t>(config_.BucketSlots(), -1)),
      ctx_(TFHEContext::CreateClientContext(config_)),
      net_io_(std::make_unique<network::NetIO>(server_address, port, false, false)),
      prng_(0x4F6E696F6EULL) {
    std::vector<uint8_t> shared_key(crypto::AES_CTR::kKeySize128, 0x42);
    cipher_ = std::make_unique<crypto::AES_CTR>(shared_key);

    for (size_t i = 0; i < pos_map_.size(); ++i) {
        pos_map_[i] = GetRandomLeaf();
    }
}

OnionRingClient::~OnionRingClient() {
    try {
        char quit = 'Q';
        net_io_->SendData(&quit, 1);
        net_io_->Flush();
    } catch (...) {
    }
}

uint64_t OnionRingClient::GetRandomLeaf() {
    std::uniform_int_distribution<uint64_t> dist(0, num_leaves_ - 1);
    return dist(prng_);
}

uint64_t OnionRingClient::ReverseBits(uint64_t value, size_t num_bits) {
    uint64_t reversed = 0;
    for (size_t i = 0; i < num_bits; ++i) {
        reversed = (reversed << 1U) | ((value >> i) & 1ULL);
    }
    return reversed;
}

std::vector<size_t> OnionRingClient::GetPathIndices(uint64_t leaf) const {
    std::vector<size_t> path;
    path.reserve(config_.tree_height + 1);

    size_t node = 0;
    path.push_back(node);
    for (size_t level = 0; level < config_.tree_height; ++level) {
        const size_t bit = (leaf >> (config_.tree_height - 1 - level)) & 1ULL;
        node = 2 * node + 1 + bit;
        path.push_back(node);
    }
    return path;
}

size_t OnionRingClient::GetNodeLevel(size_t bucket_idx) const {
    size_t level = 0;
    while (bucket_idx > 0) {
        bucket_idx = (bucket_idx - 1) / 2;
        ++level;
    }
    return level;
}

size_t OnionRingClient::FindSlot(size_t bucket_idx, int64_t block_id) const {
    for (size_t slot = 0; slot < id_map_[bucket_idx].size(); ++slot) {
        if (id_map_[bucket_idx][slot] == block_id) {
            return slot;
        }
    }
    return id_map_[bucket_idx].size();
}

size_t OnionRingClient::FindFirstDummySlot(size_t bucket_idx) const {
    return FindSlot(bucket_idx, -1);
}

RLWECiphertext OnionRingClient::FetchPathSum(uint64_t leaf, const std::vector<size_t>& selections) {
    char command = 'A';
    net_io_->SendData(&command, 1);
    net_io_->SendData(&leaf, sizeof(leaf));
    for (size_t slot : selections) {
        uint64_t slot_u64 = slot;
        net_io_->SendData(&slot_u64, sizeof(slot_u64));
    }
    net_io_->Flush();

    std::vector<uint8_t> bytes;
    net_io_->RecvVec(bytes);
    return RLWECiphertext::Deserialize(bytes, ctx_.tlwe_params);
}

std::vector<RLWECiphertext> OnionRingClient::FetchBucketCiphertexts(size_t bucket_idx) {
    char command = 'R';
    uint64_t bucket_u64 = bucket_idx;
    net_io_->SendData(&command, 1);
    net_io_->SendData(&bucket_u64, sizeof(bucket_u64));
    net_io_->Flush();

    std::vector<RLWECiphertext> ciphertexts;
    ciphertexts.reserve(config_.BucketSlots());
    for (size_t slot = 0; slot < config_.BucketSlots(); ++slot) {
        std::vector<uint8_t> bytes;
        net_io_->RecvVec(bytes);
        ciphertexts.emplace_back(RLWECiphertext::Deserialize(bytes, ctx_.tlwe_params));
    }
    return ciphertexts;
}

void OnionRingClient::ClearPathSlots(uint64_t leaf, const std::vector<size_t>& selections) {
    char command = 'C';
    net_io_->SendData(&command, 1);
    net_io_->SendData(&leaf, sizeof(leaf));
    for (size_t slot : selections) {
        uint64_t slot_u64 = slot;
        net_io_->SendData(&slot_u64, sizeof(slot_u64));
    }
    net_io_->Flush();

    char ack = 0;
    net_io_->RecvData(&ack, sizeof(ack));
    if (ack != 'K') {
        throw std::runtime_error("Unexpected Onion Ring clear-path acknowledgment");
    }
}

void OnionRingClient::WriteBackSlot(size_t bucket_idx, size_t slot_idx,
                                    const RLWECiphertext& ciphertext) {
    char command = 'U';
    uint64_t bucket_u64 = bucket_idx;
    uint64_t slot_u64 = slot_idx;
    net_io_->SendData(&command, 1);
    net_io_->SendData(&bucket_u64, sizeof(bucket_u64));
    net_io_->SendData(&slot_u64, sizeof(slot_u64));
    net_io_->SendVec(ciphertext.Serialize());
    net_io_->Flush();

    char ack = 0;
    net_io_->RecvData(&ack, sizeof(ack));
    if (ack != 'K') {
        throw std::runtime_error("Unexpected Onion Ring write-back acknowledgment");
    }
}

void OnionRingClient::WriteBucketCiphertexts(size_t bucket_idx,
                                             const std::vector<RLWECiphertext>& ciphertexts) {
    if (ciphertexts.size() != config_.BucketSlots()) {
        throw std::invalid_argument("Bucket ciphertext count mismatch");
    }
    for (size_t slot = 0; slot < ciphertexts.size(); ++slot) {
        WriteBackSlot(bucket_idx, slot, ciphertexts[slot]);
    }
}

void OnionRingClient::TripletEvict(size_t source_idx, size_t left_idx, size_t right_idx) {
    const size_t parent_level = GetNodeLevel(source_idx);
    const size_t child_bit_shift = config_.tree_height - parent_level - 1;
    const size_t bucket_slots = config_.BucketSlots();

    // The prototype keeps the TFHE/Waksman server-side machinery implemented and tested,
    // but rebuilds triplet outputs client-side so the end-to-end protocol stays stable
    // while the packed-swap-bit/HomExpand path remains a follow-up optimization.
    const auto source_bucket = FetchBucketCiphertexts(source_idx);
    const auto left_child_bucket = FetchBucketCiphertexts(left_idx);
    const auto right_child_bucket = FetchBucketCiphertexts(right_idx);

    std::vector<int64_t> left_input_ids;
    std::vector<int64_t> right_input_ids;
    std::vector<RLWECiphertext> left_input_ciphertexts;
    std::vector<RLWECiphertext> right_input_ciphertexts;

    for (size_t slot = 0; slot < bucket_slots; ++slot) {
        const int64_t block_id = id_map_[source_idx][slot];
        if (block_id < 0) {
            continue;
        }
        const bool goes_right = ((pos_map_[static_cast<size_t>(block_id)] >> child_bit_shift) &
                                 1ULL) != 0;
        if (goes_right) {
            right_input_ids.push_back(block_id);
            right_input_ciphertexts.emplace_back(CloneCiphertext(source_bucket[slot], ctx_.tlwe_params));
        } else {
            left_input_ids.push_back(block_id);
            left_input_ciphertexts.emplace_back(CloneCiphertext(source_bucket[slot], ctx_.tlwe_params));
        }
    }

    for (size_t slot = 0; slot < bucket_slots; ++slot) {
        if (id_map_[left_idx][slot] >= 0) {
            left_input_ids.push_back(id_map_[left_idx][slot]);
            left_input_ciphertexts.emplace_back(
                CloneCiphertext(left_child_bucket[slot], ctx_.tlwe_params));
        }
        if (id_map_[right_idx][slot] >= 0) {
            right_input_ids.push_back(id_map_[right_idx][slot]);
            right_input_ciphertexts.emplace_back(
                CloneCiphertext(right_child_bucket[slot], ctx_.tlwe_params));
        }
    }

    if (left_input_ids.size() > bucket_slots || right_input_ids.size() > bucket_slots) {
        throw std::runtime_error("Triplet eviction overflowed child bucket capacity");
    }

    while (left_input_ids.size() < bucket_slots) {
        left_input_ids.push_back(-1);
        left_input_ciphertexts.emplace_back(ZeroCiphertext(ctx_.tlwe_params));
    }
    while (right_input_ids.size() < bucket_slots) {
        right_input_ids.push_back(-1);
        right_input_ciphertexts.emplace_back(ZeroCiphertext(ctx_.tlwe_params));
    }

    const auto left_permutation = MakeRandomPermutation(bucket_slots, &prng_);
    const auto right_permutation = MakeRandomPermutation(bucket_slots, &prng_);

    std::vector<RLWECiphertext> left_output_ciphertexts;
    std::vector<RLWECiphertext> right_output_ciphertexts;
    left_output_ciphertexts.reserve(bucket_slots);
    right_output_ciphertexts.reserve(bucket_slots);
    for (size_t dest = 0; dest < bucket_slots; ++dest) {
        left_output_ciphertexts.emplace_back(
            CloneCiphertext(left_input_ciphertexts[left_permutation[dest]], ctx_.tlwe_params));
        right_output_ciphertexts.emplace_back(
            CloneCiphertext(right_input_ciphertexts[right_permutation[dest]], ctx_.tlwe_params));
    }

    std::vector<RLWECiphertext> cleared_source;
    cleared_source.reserve(bucket_slots);
    for (size_t slot = 0; slot < bucket_slots; ++slot) {
        cleared_source.emplace_back(ZeroCiphertext(ctx_.tlwe_params));
    }

    WriteBucketCiphertexts(left_idx, left_output_ciphertexts);
    WriteBucketCiphertexts(right_idx, right_output_ciphertexts);
    WriteBucketCiphertexts(source_idx, cleared_source);

    std::fill(id_map_[source_idx].begin(), id_map_[source_idx].end(), -1);
    id_map_[left_idx] = ApplyPermutationToIds(left_input_ids, left_permutation);
    id_map_[right_idx] = ApplyPermutationToIds(right_input_ids, right_permutation);
}

void OnionRingClient::LeafRefresh(size_t leaf_bucket_idx) {
    // Leaf refresh follows the same client-assisted rebuild strategy as triplet eviction:
    // decrypt, re-encrypt with fresh randomness, reshuffle locally, then write back.
    const auto bucket = FetchBucketCiphertexts(leaf_bucket_idx);
    std::vector<RLWECiphertext> refreshed_input;
    refreshed_input.reserve(config_.BucketSlots());
    for (const auto& ciphertext : bucket) {
        const auto plaintext = DecryptBlock(ciphertext, ctx_, config_.block_size);
        refreshed_input.emplace_back(EncryptBlock(plaintext, ctx_));
    }
    const auto permutation = MakeRandomPermutation(config_.BucketSlots(), &prng_);
    const auto current_ids = id_map_[leaf_bucket_idx];
    std::vector<RLWECiphertext> refreshed_output;
    refreshed_output.reserve(config_.BucketSlots());
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        refreshed_output.emplace_back(
            CloneCiphertext(refreshed_input[permutation[dest]], ctx_.tlwe_params));
    }

    WriteBucketCiphertexts(leaf_bucket_idx, refreshed_output);
    id_map_[leaf_bucket_idx] = ApplyPermutationToIds(current_ids, permutation);
}

void OnionRingClient::Evict() {
    const uint64_t eviction_leaf = ReverseBits(eviction_counter_ % num_leaves_, config_.tree_height);
    ++eviction_counter_;

    auto path = GetPathIndices(eviction_leaf);
    for (size_t depth = 0; depth + 1 < path.size(); ++depth) {
        const size_t source_idx = path[depth];
        TripletEvict(source_idx, 2 * source_idx + 1, 2 * source_idx + 2);
    }
    LeafRefresh(path.back());
}

std::vector<uint8_t> OnionRingClient::Access(core::Op op, uint64_t addr,
                                             const std::vector<uint8_t>& data) {
    if (addr >= config_.num_blocks) {
        throw std::out_of_range("Address out of range");
    }
    if (op == core::Op::WRITE && data.size() != config_.block_size) {
        throw std::invalid_argument("Write size does not match block size");
    }

    const uint64_t old_leaf = pos_map_[addr];
    pos_map_[addr] = GetRandomLeaf();

    auto path = GetPathIndices(old_leaf);
    std::vector<size_t> selections;
    selections.reserve(path.size());
    for (size_t bucket_idx : path) {
        size_t slot = FindSlot(bucket_idx, static_cast<int64_t>(addr));
        if (slot == id_map_[bucket_idx].size()) {
            slot = FindFirstDummySlot(bucket_idx);
        }
        if (slot == id_map_[bucket_idx].size()) {
            throw std::runtime_error("No available slot for access-only Onion Ring path");
        }
        selections.push_back(slot);
    }

    RLWECiphertext path_sum = FetchPathSum(old_leaf, selections);
    ClearPathSlots(old_leaf, selections);
    for (size_t i = 0; i < path.size(); ++i) {
        id_map_[path[i]][selections[i]] = -1;
    }

    std::vector<uint8_t> result = DecryptBlock(path_sum, ctx_, config_.block_size);
    if (op == core::Op::WRITE) {
        result = data;
    }

    size_t root_slot = FindSlot(0, static_cast<int64_t>(addr));
    if (root_slot == id_map_[0].size()) {
        root_slot = FindFirstDummySlot(0);
    }
    if (root_slot == id_map_[0].size()) {
        Evict();
        root_slot = FindFirstDummySlot(0);
    }
    if (root_slot == id_map_[0].size()) {
        throw std::runtime_error("Root bucket is still full after eviction");
    }

    RLWECiphertext updated = EncryptBlock(result, ctx_);
    WriteBackSlot(0, root_slot, updated);
    id_map_[0][root_slot] = static_cast<int64_t>(addr);

    ++access_count_;
    if (config_.a != 0 && access_count_ % config_.a == 0) {
        Evict();
    }
    return result;
}

std::vector<uint8_t> OnionRingClient::Read(uint64_t addr) { return Access(core::Op::READ, addr, {}); }

void OnionRingClient::Write(uint64_t addr, const std::vector<uint8_t>& data) {
    Access(core::Op::WRITE, addr, data);
}

}  // namespace oram::onion_ring
