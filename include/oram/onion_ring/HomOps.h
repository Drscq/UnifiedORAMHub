#pragma once

#include "tfhe.h"

namespace oram::onion_ring {

void ExternalProduct(TLweSample* result, const TGswSample* rgsw, const TLweSample* rlwe,
                     const TGswParams* params);
void ExternalProductWithParams(TLweSample* result, const TGswSample* rgsw,
                               const TLweSample* rlwe, const TGswParams* params);

void CMux(TLweSample* result, const TGswSample* control, const TLweSample* d1,
          const TLweSample* d0, const TGswParams* params);

void Subs(TLweSample* result, const TLweSample* input, int32_t ai, const TLweParams* params);

}  // namespace oram::onion_ring
