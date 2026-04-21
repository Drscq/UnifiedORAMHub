#pragma once

#include <cstddef>
#include <vector>

#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

struct Slot {
    explicit Slot(const TLweParams* params) : rlwe_ct(params) {}

    RLWECiphertext rlwe_ct;
    std::vector<uint8_t> aes_layer;
};

class OnionBucket {
   public:
    OnionBucket(size_t slot_count, const TLweParams* params) {
        slots_.reserve(slot_count);
        for (size_t i = 0; i < slot_count; ++i) {
            slots_.emplace_back(params);
        }
    }

    Slot& operator[](size_t index) { return slots_.at(index); }
    const Slot& operator[](size_t index) const { return slots_.at(index); }
    size_t Size() const { return slots_.size(); }

   private:
    std::vector<Slot> slots_;
};

}  // namespace oram::onion_ring
