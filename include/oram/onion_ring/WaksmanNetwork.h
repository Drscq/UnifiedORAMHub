#pragma once

#include <cstddef>
#include <vector>

#include "tfhe.h"

namespace oram::onion_ring {

struct WaksmanGate {
    size_t i;
    size_t j;
    size_t bit_idx;
};

class WaksmanNetwork {
   public:
    explicit WaksmanNetwork(size_t size);

    std::vector<bool> GenerateSwapBits(const std::vector<size_t>& permutation) const;

    const std::vector<WaksmanGate>& Gates() const { return gates_; }
    size_t NumGates() const { return gates_.size(); }

    static void EvalWaksman(std::vector<TLweSample*>& data,
                            const std::vector<TGswSample*>& swap_bits,
                            const TGswParams* params);

   private:
    size_t size_;
    std::vector<WaksmanGate> gates_;

    // The prototype keeps a Waksman-shaped interface but uses a fixed adjacent-swap
    // oblivious network internally so the higher-level protocol can stabilize first.
    void BuildNetwork();
};

}  // namespace oram::onion_ring
