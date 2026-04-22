#include "oram/onion_ring/HomExpand.h"

#include <stdexcept>

namespace oram::onion_ring {

std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx) {
    (void)payload;
    (void)bundle;
    (void)server_ctx;
    throw std::runtime_error("HomExpandPackedSwapBits is not implemented yet");
}

}  // namespace oram::onion_ring
