#include "oram/onion_ring/OnionRingServer.h"

#include <stdexcept>

#include "tlwe_functions.h"

namespace oram::onion_ring {

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

void OnionRingServer::HandleRequests() {
    running_ = true;
    while (running_) {
        try {
            char command = 0;
            net_io_->RecvData(&command, 1);

            if (command == 'Q') {
                break;
            }
            if (command == 'A') {
                HandleAccess();
            } else if (command == 'U') {
                HandleWriteSlot();
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
