#include "oram/onion_ring/OnionRingServer.h"

#include <cstring>
#include <stdexcept>

#include "tlwe_functions.h"
#include "oram/onion_ring/HomExpand.h"
#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {

namespace {

std::vector<uint64_t> DeserializeUint64Vector(const std::vector<uint8_t>& bytes) {
    if (bytes.size() % sizeof(uint64_t) != 0) {
        throw std::invalid_argument("Serialized uint64 vector has invalid size");
    }

    std::vector<uint64_t> values(bytes.size() / sizeof(uint64_t), 0);
    if (!bytes.empty()) {
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
}

std::vector<RLWECiphertext> AssembleChildSlots(const OnionBucket& source_bucket,
                                               const std::vector<uint64_t>& source_slots,
                                               const OnionBucket& child_bucket,
                                               const std::vector<uint64_t>& child_slots,
                                               size_t bucket_slots, const TLweParams* params) {
    std::vector<RLWECiphertext> assembled;
    assembled.reserve(bucket_slots);

    for (uint64_t slot_idx : source_slots) {
        if (slot_idx >= source_bucket.Size()) {
            throw std::out_of_range("Source slot index out of range");
        }
        assembled.emplace_back(params);
        tLweCopy(assembled.back().Get(), source_bucket[slot_idx].rlwe_ct.Get(), params);
    }

    for (uint64_t slot_idx : child_slots) {
        if (slot_idx >= child_bucket.Size()) {
            throw std::out_of_range("Child slot index out of range");
        }
        assembled.emplace_back(params);
        tLweCopy(assembled.back().Get(), child_bucket[slot_idx].rlwe_ct.Get(), params);
    }

    while (assembled.size() < bucket_slots) {
        assembled.emplace_back(params);
        tLweClear(assembled.back().Get(), params);
    }
    return assembled;
}

void ReplaceBucket(OnionBucket* bucket, std::vector<RLWECiphertext>* contents,
                   const TLweParams* params) {
    if (bucket->Size() != contents->size()) {
        throw std::invalid_argument("Bucket replacement size mismatch");
    }
    for (size_t slot = 0; slot < bucket->Size(); ++slot) {
        tLweCopy((*bucket)[slot].rlwe_ct.Get(), (*contents)[slot].Get(), params);
    }
}

const TGswParams* ProtocolControlParams(const RuntimeConfig& config, const TFHEContext& ctx) {
    return config.use_recursive_packed_swap_bits ? ctx.swap_tgsw_params : ctx.practical_tgsw_params;
}

}  // namespace

OnionRingServer::OnionRingServer(const std::string& address, int port, const RuntimeConfig& config)
    : config_(config),
      num_nodes_(config_.NumTreeNodes()),
      ctx_(TFHEContext::CreateServerContext(config_)),
      net_io_(std::make_unique<network::NetIO>(address, port, true, false)) {}

OnionRingServer::~OnionRingServer() { Stop(); }

void OnionRingServer::Init() {
    tree_.clear();
    tree_.reserve(num_nodes_);
    for (size_t i = 0; i < num_nodes_; ++i) {
        tree_.emplace_back(config_.BucketSlots(), ctx_.tlwe_params);
        for (size_t slot = 0; slot < tree_.back().Size(); ++slot) {
            tLweClear(tree_.back()[slot].rlwe_ct.Get(), ctx_.tlwe_params);
        }
    }
}

std::vector<size_t> OnionRingServer::GetPathIndices(uint64_t leaf) const {
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

void OnionRingServer::HandleAccess() {
    uint64_t leaf = 0;
    net_io_->RecvData(&leaf, sizeof(leaf));

    auto path = GetPathIndices(leaf);
    std::vector<uint64_t> selections(path.size(), 0);
    for (size_t i = 0; i < path.size(); ++i) {
        net_io_->RecvData(&selections[i], sizeof(selections[i]));
        if (selections[i] >= config_.BucketSlots()) {
            throw std::out_of_range("Selection index out of range");
        }
    }

    RLWECiphertext accumulator(ctx_.tlwe_params);
    tLweClear(accumulator.Get(), ctx_.tlwe_params);
    for (size_t i = 0; i < path.size(); ++i) {
        tLweAddTo(accumulator.Get(), tree_[path[i]][selections[i]].rlwe_ct.Get(), ctx_.tlwe_params);
    }

    net_io_->SendVec(accumulator.Serialize());
    net_io_->Flush();
}

void OnionRingServer::HandleExpansionSupport() {
    std::vector<uint8_t> bundle_bytes;
    net_io_->RecvVec(bundle_bytes);
    expansion_bundle_ =
        ExpansionBundle::Deserialize(bundle_bytes, config_, ctx_.tlwe_params, ctx_.swap_tgsw_params);
    if (expansion_bundle_.neg_sk_rgsw_bytes.empty()) {
        throw std::runtime_error("Packed-support setup did not provide RGSW(-s) bytes");
    }
    if (expansion_bundle_.recursive_ks_keys.empty()) {
        throw std::runtime_error("Packed-support setup did not provide recursive RLWE key-switch material");
    }

    char ack = 'K';
    net_io_->SendData(&ack, sizeof(ack));
    net_io_->Flush();
}

void OnionRingServer::HandleClearPath() {
    uint64_t leaf = 0;
    net_io_->RecvData(&leaf, sizeof(leaf));

    auto path = GetPathIndices(leaf);
    std::vector<uint64_t> selections(path.size(), 0);
    for (size_t i = 0; i < path.size(); ++i) {
        net_io_->RecvData(&selections[i], sizeof(selections[i]));
        if (selections[i] >= config_.BucketSlots()) {
            throw std::out_of_range("Clear-path selection index out of range");
        }
        tLweClear(tree_[path[i]][selections[i]].rlwe_ct.Get(), ctx_.tlwe_params);
    }

    char ack = 'K';
    net_io_->SendData(&ack, sizeof(ack));
    net_io_->Flush();
}

void OnionRingServer::HandleReadBucket() {
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Read bucket index out of range");
    }

    for (size_t slot = 0; slot < tree_[bucket_idx].Size(); ++slot) {
        net_io_->SendVec(tree_[bucket_idx][slot].rlwe_ct.Serialize());
    }
    net_io_->Flush();
}

void OnionRingServer::HandleWriteSlot() {
    uint64_t bucket_idx = 0;
    uint64_t slot_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    net_io_->RecvData(&slot_idx, sizeof(slot_idx));

    if (bucket_idx >= tree_.size() || slot_idx >= tree_[bucket_idx].Size()) {
        throw std::out_of_range("Write slot target out of range");
    }

    std::vector<uint8_t> bytes;
    net_io_->RecvVec(bytes);
    tree_[bucket_idx][slot_idx].rlwe_ct = RLWECiphertext::Deserialize(bytes, ctx_.tlwe_params);

    char ack = 'K';
    net_io_->SendData(&ack, sizeof(ack));
    net_io_->Flush();
}

void OnionRingServer::HandleEvictTriplet() {
    uint64_t source_idx = 0;
    uint64_t left_idx = 0;
    uint64_t right_idx = 0;
    net_io_->RecvData(&source_idx, sizeof(source_idx));
    net_io_->RecvData(&left_idx, sizeof(left_idx));
    net_io_->RecvData(&right_idx, sizeof(right_idx));

    if (source_idx >= tree_.size() || left_idx >= tree_.size() || right_idx >= tree_.size()) {
        throw std::out_of_range("Triplet bucket index out of range");
    }

    auto receive_child_update = [&](size_t child_idx) {
        std::vector<uint8_t> source_bytes;
        std::vector<uint8_t> child_bytes;
        net_io_->RecvVec(source_bytes);
        net_io_->RecvVec(child_bytes);
        const auto source_slots = DeserializeUint64Vector(source_bytes);
        const auto child_slots = DeserializeUint64Vector(child_bytes);

        auto swap_bits = HomExpandPackedSwapBits(RecvPackedSwapBitPayload(net_io_.get()),
                                                 expansion_bundle_, ctx_);

        std::vector<RLWECiphertext> assembled =
            AssembleChildSlots(tree_[source_idx], source_slots, tree_[child_idx], child_slots,
                               config_.BucketSlots(), ctx_.tlwe_params);
        std::vector<TLweSample*> slot_ptrs;
        slot_ptrs.reserve(assembled.size());
        for (auto& slot : assembled) {
            slot_ptrs.push_back(slot.Get());
        }

        std::vector<TGswSample*> swap_ptrs;
        swap_ptrs.reserve(swap_bits.size());
        for (auto& bit : swap_bits) {
            swap_ptrs.push_back(bit.Get());
        }

        WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, ProtocolControlParams(config_, ctx_));
        ReplaceBucket(&tree_[child_idx], &assembled, ctx_.tlwe_params);
    };

    receive_child_update(left_idx);
    receive_child_update(right_idx);

    for (size_t slot = 0; slot < tree_[source_idx].Size(); ++slot) {
        tLweClear(tree_[source_idx][slot].rlwe_ct.Get(), ctx_.tlwe_params);
    }

    char ack = 'K';
    net_io_->SendData(&ack, sizeof(ack));
    net_io_->Flush();
}

void OnionRingServer::HandleLeafRefresh() {
    uint64_t bucket_idx = 0;
    net_io_->RecvData(&bucket_idx, sizeof(bucket_idx));
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Leaf refresh bucket index out of range");
    }

    char bucket_marker = 'B';
    net_io_->SendData(&bucket_marker, sizeof(bucket_marker));
    for (size_t slot = 0; slot < tree_[bucket_idx].Size(); ++slot) {
        net_io_->SendVec(tree_[bucket_idx][slot].rlwe_ct.Serialize());
    }
    net_io_->Flush();

    char refresh_command = 0;
    net_io_->RecvData(&refresh_command, sizeof(refresh_command));
    if (refresh_command != 'F') {
        throw std::runtime_error("Expected leaf refresh payload command");
    }

    std::vector<RLWECiphertext> refreshed;
    refreshed.reserve(config_.BucketSlots());
    for (size_t slot = 0; slot < config_.BucketSlots(); ++slot) {
        std::vector<uint8_t> bytes;
        net_io_->RecvVec(bytes);
        refreshed.emplace_back(RLWECiphertext::Deserialize(bytes, ctx_.tlwe_params));
    }

    auto swap_bits =
        HomExpandPackedSwapBits(RecvPackedSwapBitPayload(net_io_.get()), expansion_bundle_, ctx_);

    std::vector<TLweSample*> slot_ptrs;
    slot_ptrs.reserve(refreshed.size());
    for (auto& slot : refreshed) {
        slot_ptrs.push_back(slot.Get());
    }

    std::vector<TGswSample*> swap_ptrs;
    swap_ptrs.reserve(swap_bits.size());
    for (auto& bit : swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }

    WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, ProtocolControlParams(config_, ctx_));
    ReplaceBucket(&tree_[bucket_idx], &refreshed, ctx_.tlwe_params);

    char ack = 'K';
    net_io_->SendData(&ack, sizeof(ack));
    net_io_->Flush();
}

void OnionRingServer::HandleRequests() {
    running_ = true;
    while (running_) {
        try {
            char command = 0;
            net_io_->RecvData(&command, 1);

            if (command == 'Q') {
                break;
            }
            if (command == 'E') {
                HandleExpansionSupport();
            } else if (command == 'A') {
                HandleAccess();
            } else if (command == 'C') {
                HandleClearPath();
            } else if (command == 'R') {
                HandleReadBucket();
            } else if (command == 'U') {
                HandleWriteSlot();
            } else if (command == 'T') {
                HandleEvictTriplet();
            } else if (command == 'L') {
                HandleLeafRefresh();
            } else {
                throw std::runtime_error("Unknown Onion Ring server command");
            }
        } catch (...) {
            break;
        }
    }
    running_ = false;
}

void OnionRingServer::Stop() { running_ = false; }

}  // namespace oram::onion_ring
