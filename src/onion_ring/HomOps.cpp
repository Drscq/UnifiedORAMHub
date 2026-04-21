#include "oram/onion_ring/HomOps.h"

#include <stdexcept>

#include "polynomials_arithmetic.h"
#include "tlwe_functions.h"
#include "tgsw_functions.h"

namespace oram::onion_ring {

void ExternalProduct(TLweSample* result, const TGswSample* rgsw, const TLweSample* rlwe,
                     const TGswParams* params) {
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
    if (ai < 0 || ai >= 2 * params->N) {
        throw std::out_of_range("Subs exponent must be in [0, 2N)");
    }

    for (int poly = 0; poly <= params->k; ++poly) {
        torusPolynomialMulByXai(&result->a[poly], ai, &input->a[poly]);
    }
    result->current_variance = input->current_variance;
}

}  // namespace oram::onion_ring
