#include "oram/onion_ring/HomOps.h"

#include <cstdint>
#include <stdexcept>

#include "polynomials_arithmetic.h"
#include "tlwe_functions.h"
#include "tgsw_functions.h"

namespace oram::onion_ring {

namespace {

void ApplyAutomorphism(TorusPolynomial* result, const TorusPolynomial* input, int32_t power) {
    const int n = input->N;
    if ((power & 1) == 0) {
        throw std::invalid_argument("Subs exponent must be odd");
    }

    torusPolynomialClear(result);
    for (int coeff = 0; coeff < n; ++coeff) {
        const int64_t mapped = (static_cast<int64_t>(coeff) * power) % (2LL * n);
        if (mapped < n) {
            result->coefsT[mapped] = input->coefsT[coeff];
        } else {
            result->coefsT[mapped - n] = -input->coefsT[coeff];
        }
    }
}

}  // namespace

void ExternalProduct(TLweSample* result, const TGswSample* rgsw, const TLweSample* rlwe,
                     const TGswParams* params) {
    ExternalProductWithParams(result, rgsw, rlwe, params);
}

void ExternalProductWithParams(TLweSample* result, const TGswSample* rgsw,
                               const TLweSample* rlwe, const TGswParams* params) {
    tGswExternProduct(result, rgsw, rlwe, params);
}

void CMux(TLweSample* result, const TGswSample* control, const TLweSample* d1,
          const TLweSample* d0, const TGswParams* params) {
    const TLweParams* tlwe_params = params->tlwe_params;
    TLweSample* diff = new_TLweSample(tlwe_params);

    tLweCopy(diff, d1, tlwe_params);
    tLweSubTo(diff, d0, tlwe_params);
    tGswExternProduct(result, control, diff, params);
    tLweAddTo(result, d0, tlwe_params);

    delete_TLweSample(diff);
}

void Subs(TLweSample* result, const TLweSample* input, int32_t ai, const TLweParams* params) {
    if (ai <= 0 || ai >= 2 * params->N || (ai & 1) == 0) {
        throw std::out_of_range("Subs exponent must be an odd integer in (0, 2N)");
    }

    for (int poly = 0; poly <= params->k; ++poly) {
        ApplyAutomorphism(&result->a[poly], &input->a[poly], ai);
    }
    result->current_variance = input->current_variance;
}

}  // namespace oram::onion_ring
