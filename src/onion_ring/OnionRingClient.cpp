#include "oram/onion_ring/OnionRingClient.h"

#include <random>
#include <stdexcept>

namespace oram::onion_ring {

OnionRingClient::OnionRingClient(const std::string& server_address, int port,
                                 const RuntimeConfig& config)
    : config_(config),
      num_nodes_(config_.NumTreeNodes()),
      num_leaves_(config_.NumLeaves()),
      pos_map_(config_.num_blocks, 0),
      id_map_(num_nodes_, std::vector<int64_t>(config_.BucketSlots(), -1)),
      ctx_(TFHEContext::CreateClientContext(config_)),
      net_io_(std::make_unique<network::NetIO>(server_address, port, false, false)) {
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
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, num_leaves_ - 1);
    return dist(gen);
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
    std::vector<uint8_t> result = DecryptBlock(path_sum, ctx_, config_.block_size);
    if (op == core::Op::WRITE) {
        result = data;
    }

    size_t root_slot = FindSlot(0, static_cast<int64_t>(addr));
    if (root_slot == id_map_[0].size()) {
        root_slot = FindFirstDummySlot(0);
    }
    if (root_slot == id_map_[0].size()) {
        throw std::runtime_error("Root bucket is full before eviction is implemented");
    }

    RLWECiphertext updated = EncryptBlock(result, ctx_);
    WriteBackSlot(0, root_slot, updated);
    id_map_[0][root_slot] = static_cast<int64_t>(addr);

    ++access_count_;
    return result;
}

std::vector<uint8_t> OnionRingClient::Read(uint64_t addr) { return Access(core::Op::READ, addr, {}); }

void OnionRingClient::Write(uint64_t addr, const std::vector<uint8_t>& data) {
    Access(core::Op::WRITE, addr, data);
}

}  // namespace oram::onion_ring
