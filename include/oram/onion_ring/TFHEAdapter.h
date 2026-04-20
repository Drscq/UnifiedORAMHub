#pragma once

#include "oram/onion_ring/Config.h"

#include "tfhe.h"

namespace oram::onion_ring {

struct TFHEContext {
    TLweParams* tlwe_params = nullptr;
    TGswParams* tgsw_params = nullptr;
    TLweKey* tlwe_key = nullptr;
    TGswKey* tgsw_key = nullptr;
    double alpha = 0.0;

    TFHEContext() = default;
    ~TFHEContext();

    TFHEContext(const TFHEContext&) = delete;
    TFHEContext& operator=(const TFHEContext&) = delete;

    TFHEContext(TFHEContext&& other) noexcept;
    TFHEContext& operator=(TFHEContext&& other) noexcept;

    static TFHEContext CreateClientContext(const RuntimeConfig& config);
    static TFHEContext CreateServerContext(const RuntimeConfig& config);

    void Reset();
};

}  // namespace oram::onion_ring
