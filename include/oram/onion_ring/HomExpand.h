#pragma once

#include <vector>

#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx);

}  // namespace oram::onion_ring
