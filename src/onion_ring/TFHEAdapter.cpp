#include "oram/onion_ring/TFHEAdapter.h"

#include <utility>

namespace oram::onion_ring {

namespace {

TLweParams* CreateTLweParams(const RuntimeConfig& config) {
    return new_TLweParams(config.tlwe_n, config.tlwe_k, config.alpha, 1.0 / 16.0);
}

TGswParams* CreateTGswParams(const RuntimeConfig& config, const TLweParams* tlwe_params) {
    return new_TGswParams(config.tgsw_l, config.tgsw_bgbit, tlwe_params);
}

}  // namespace

TFHEContext::~TFHEContext() { Reset(); }

TFHEContext::TFHEContext(TFHEContext&& other) noexcept
    : tlwe_params(std::exchange(other.tlwe_params, nullptr)),
      tgsw_params(std::exchange(other.tgsw_params, nullptr)),
      tlwe_key(std::exchange(other.tlwe_key, nullptr)),
      tgsw_key(std::exchange(other.tgsw_key, nullptr)),
      alpha(other.alpha) {}

TFHEContext& TFHEContext::operator=(TFHEContext&& other) noexcept {
    if (this != &other) {
        Reset();
        tlwe_params = std::exchange(other.tlwe_params, nullptr);
        tgsw_params = std::exchange(other.tgsw_params, nullptr);
        tlwe_key = std::exchange(other.tlwe_key, nullptr);
        tgsw_key = std::exchange(other.tgsw_key, nullptr);
        alpha = other.alpha;
    }
    return *this;
}

TFHEContext TFHEContext::CreateClientContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.tgsw_params = CreateTGswParams(config, ctx.tlwe_params);
    ctx.tlwe_key = new_TLweKey(ctx.tlwe_params);
    ctx.tgsw_key = new_TGswKey(ctx.tgsw_params);
    tLweKeyGen(ctx.tlwe_key);
    tGswKeyGen(ctx.tgsw_key);
    return ctx;
}

TFHEContext TFHEContext::CreateServerContext(const RuntimeConfig& config) {
    TFHEContext ctx;
    ctx.alpha = config.alpha;
    ctx.tlwe_params = CreateTLweParams(config);
    ctx.tgsw_params = CreateTGswParams(config, ctx.tlwe_params);
    return ctx;
}

void TFHEContext::Reset() {
    if (tgsw_key != nullptr) {
        delete_TGswKey(tgsw_key);
        tgsw_key = nullptr;
    }
    if (tlwe_key != nullptr) {
        delete_TLweKey(tlwe_key);
        tlwe_key = nullptr;
    }
    if (tgsw_params != nullptr) {
        delete_TGswParams(tgsw_params);
        tgsw_params = nullptr;
    }
    if (tlwe_params != nullptr) {
        delete_TLweParams(tlwe_params);
        tlwe_params = nullptr;
    }
}

}  // namespace oram::onion_ring
