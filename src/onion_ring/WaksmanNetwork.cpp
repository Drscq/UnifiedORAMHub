#include "oram/onion_ring/WaksmanNetwork.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "oram/onion_ring/HomOps.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

WaksmanNetwork::WaksmanNetwork(size_t size) : size_(size) {
    if (size_ < 2) {
        throw std::invalid_argument("WaksmanNetwork size must be at least 2");
    }
    BuildNetwork();
}

void WaksmanNetwork::BuildNetwork() {
    gates_.clear();
    size_t bit_idx = 0;
    for (size_t phase = 0; phase < size_; ++phase) {
        const size_t start = phase % 2;
        for (size_t i = start; i + 1 < size_; i += 2) {
            gates_.push_back(WaksmanGate{i, i + 1, bit_idx++});
        }
    }
}

std::vector<bool> WaksmanNetwork::GenerateSwapBits(const std::vector<size_t>& permutation) const {
    if (permutation.size() != size_) {
        throw std::invalid_argument("Permutation size mismatch");
    }

    std::vector<bool> seen(size_, false);
    std::vector<size_t> rank(size_, 0);
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        if (permutation[dest] >= size_ || seen[permutation[dest]]) {
            throw std::invalid_argument("Permutation must contain each index exactly once");
        }
        seen[permutation[dest]] = true;
        rank[permutation[dest]] = dest;
    }

    std::vector<size_t> current(size_);
    for (size_t i = 0; i < size_; ++i) {
        current[i] = i;
    }

    std::vector<bool> swap_bits(gates_.size(), false);
    for (const auto& gate : gates_) {
        const bool should_swap = rank[current[gate.i]] > rank[current[gate.j]];
        swap_bits[gate.bit_idx] = should_swap;
        if (should_swap) {
            std::swap(current[gate.i], current[gate.j]);
        }
    }

    if (current != permutation) {
        throw std::runtime_error("Failed to synthesize permutation with fixed swap network");
    }
    return swap_bits;
}

void WaksmanNetwork::EvalWaksman(std::vector<TLweSample*>& data,
                                 const std::vector<TGswSample*>& swap_bits,
                                 const TGswParams* params) {
    if (data.size() < 2) {
        throw std::invalid_argument("EvalWaksman requires at least two elements");
    }

    const TLweParams* tlwe_params = params->tlwe_params;
    for (size_t bit_idx = 0; bit_idx < swap_bits.size(); ++bit_idx) {
        if (swap_bits[bit_idx] == nullptr) {
            throw std::invalid_argument("Swap bit ciphertext pointer cannot be null");
        }
    }

    WaksmanNetwork network(data.size());
    if (swap_bits.size() != network.NumGates()) {
        throw std::invalid_argument("Swap bit count does not match network gate count");
    }

    for (const auto& gate : network.Gates()) {
        RLWECiphertext left_tmp(tlwe_params);
        RLWECiphertext right_tmp(tlwe_params);

        CMux(left_tmp.Get(), swap_bits[gate.bit_idx], data[gate.j], data[gate.i], params);
        CMux(right_tmp.Get(), swap_bits[gate.bit_idx], data[gate.i], data[gate.j], params);

        tLweCopy(data[gate.i], left_tmp.Get(), tlwe_params);
        tLweCopy(data[gate.j], right_tmp.Get(), tlwe_params);
    }
}

}  // namespace oram::onion_ring
