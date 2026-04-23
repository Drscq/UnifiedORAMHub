#pragma once

#include <vector>

#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

std::vector<RLWECiphertext> ExpandPackedRlweForTest(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx);
std::vector<RLWECiphertext> ExpandPackedRlweRowForTest(const PackedSwapBitPayload& payload,
                                                       const ExpansionBundle& bundle,
                                                       const TFHEContext& server_ctx,
                                                       size_t row_index);

RLWECiphertext ApplyRecursiveSubstitutionKeySwitchForTest(const RLWECiphertext& input,
                                                          const ExpansionBundle& bundle,
                                                          const TFHEContext& server_ctx,
                                                          int32_t substitution_power);

std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx);

}  // namespace oram::onion_ring
