#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "oram/onion_ring/HomOps.h"
#include "oram/onion_ring/TFHEAdapter.h"

#include "polynomials_arithmetic.h"
#include "tgsw_functions.h"
#include "tlwe_functions.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kBlockChunks = 342;
constexpr size_t kChunkBytes = 3 * 1024;
constexpr size_t kWaksmanInputs = 512;
constexpr size_t kWaksmanSwapGates = 4608;
constexpr size_t kPackedSwapCiphertexts = 16;
constexpr size_t kExpansionOutputs = 4608;
constexpr size_t kExpansionOutputsPerPackedCiphertext =
    kExpansionOutputs / kPackedSwapCiphertexts;
constexpr size_t kCmuxSampleOps = 1000;
constexpr size_t kWaksmanCmuxOps = kBlockChunks * kWaksmanSwapGates;
constexpr int32_t kPlaintextModulus = 1 << 12;

volatile uint64_t g_benchmark_sink = 0;

struct BenchmarkPlan {
    size_t block_chunks = kBlockChunks;
    size_t packed_swap_ciphertexts = kPackedSwapCiphertexts;
    size_t expansion_outputs_per_packed_ciphertext = kExpansionOutputsPerPackedCiphertext;
    size_t cmux_sample_ops = kCmuxSampleOps;
    bool quick = false;
};

oram::onion_ring::RuntimeConfig MakeBenchmarkConfig() {
    oram::onion_ring::RuntimeConfig cfg;
    cfg.block_size = kChunkBytes;
    cfg.tlwe_n = 2048;
    cfg.tlwe_k = 1;
    cfg.alpha = std::ldexp(1.0, -55);
    cfg.tgsw_bgbit = 3;
    cfg.tgsw_l = 8;
    return cfg;
}

double Milliseconds(const Clock::duration& duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

TorusPolynomial* MakePlaintextPolynomial(const TLweParams* params) {
    TorusPolynomial* plaintext = new_TorusPolynomial(params->N);
    for (int i = 0; i < params->N; ++i) {
        plaintext->coefsT[i] = modSwitchToTorus32(i % kPlaintextModulus, kPlaintextModulus);
    }
    return plaintext;
}

std::vector<oram::onion_ring::RLWECiphertext> MakeRlweArray(
    size_t count, const oram::onion_ring::TFHEContext& ctx) {
    std::vector<oram::onion_ring::RLWECiphertext> ciphertexts;
    ciphertexts.reserve(count);

    TorusPolynomial* plaintext = MakePlaintextPolynomial(ctx.tlwe_params);
    for (size_t i = 0; i < count; ++i) {
        ciphertexts.emplace_back(ctx.tlwe_params);
        tLweNoiselessTrivial(ciphertexts.back().Get(), plaintext, ctx.tlwe_params);
    }
    delete_TorusPolynomial(plaintext);

    return ciphertexts;
}

oram::onion_ring::RGSWCiphertext MakeTrivialRgswBit(bool bit,
                                                    const oram::onion_ring::TFHEContext& ctx) {
    oram::onion_ring::RGSWCiphertext ciphertext(ctx.tgsw_params);
    IntPolynomial* message = new_IntPolynomial(ctx.tlwe_params->N);
    for (int i = 0; i < ctx.tlwe_params->N; ++i) {
        message->coefs[i] = 0;
    }
    message->coefs[0] = bit ? 1 : 0;
    tGswNoiselessTrivial(ciphertext.Get(), message, ctx.tgsw_params);
    delete_IntPolynomial(message);
    return ciphertext;
}

void TLweAddMulRKaratsuba(TLweSample* result, const IntPolynomial* poly,
                          const TLweSample* sample, const TLweParams* params) {
    for (int i = 0; i <= params->k; ++i) {
        torusPolynomialAddMulRKaratsuba(result->a + i, poly, sample->a + i);
    }
    result->current_variance += intPolynomialNormSq2(poly) * sample->current_variance;
}

void TLwePhaseKaratsuba(TorusPolynomial* phase, const TLweSample* sample,
                        const TLweKey* key) {
    torusPolynomialCopy(phase, sample->b);
    for (int i = 0; i < key->params->k; ++i) {
        torusPolynomialSubMulRKaratsuba(phase, &key->key[i], &sample->a[i]);
    }
}

void TLweSymDecryptKaratsuba(TorusPolynomial* plaintext, const TLweSample* sample,
                             const TLweKey* key, int32_t plaintext_modulus) {
    TLwePhaseKaratsuba(plaintext, sample, key);
    for (int i = 0; i < key->params->N; ++i) {
        plaintext->coefsT[i] =
            modSwitchToTorus32(modSwitchFromTorus32(plaintext->coefsT[i], plaintext_modulus),
                               plaintext_modulus);
    }
}

void ExternalProductKaratsuba(TLweSample* result, const TGswSample* rgsw,
                              const TLweSample* rlwe, const TGswParams* params) {
    const TLweParams* tlwe_params = params->tlwe_params;
    IntPolynomial* decomposition = new_IntPolynomial_array(params->kpl, tlwe_params->N);

    tGswTLweDecompH(decomposition, rlwe, params);
    tLweClear(result, tlwe_params);
    for (int i = 0; i < params->kpl; ++i) {
        TLweAddMulRKaratsuba(result, &decomposition[i], &rgsw->all_sample[i], tlwe_params);
    }
    result->current_variance += rlwe->current_variance;

    delete_IntPolynomial_array(params->kpl, decomposition);
}

void CMuxKaratsuba(TLweSample* result, const TGswSample* control, const TLweSample* d1,
                   const TLweSample* d0, const TGswParams* params) {
    const TLweParams* tlwe_params = params->tlwe_params;
    oram::onion_ring::RLWECiphertext diff(tlwe_params);

    tLweCopy(diff.Get(), d1, tlwe_params);
    tLweSubTo(diff.Get(), d0, tlwe_params);
    ExternalProductKaratsuba(result, control, diff.Get(), params);
    tLweAddTo(result, d0, tlwe_params);
}

double BenchmarkServerAdd(const oram::onion_ring::TFHEContext& ctx, const BenchmarkPlan& plan) {
    auto lhs = MakeRlweArray(plan.block_chunks, ctx);
    auto rhs = MakeRlweArray(plan.block_chunks, ctx);
    std::vector<oram::onion_ring::RLWECiphertext> out;
    out.reserve(plan.block_chunks);
    for (size_t i = 0; i < plan.block_chunks; ++i) {
        out.emplace_back(ctx.tlwe_params);
    }

    const auto start = Clock::now();
    for (size_t i = 0; i < plan.block_chunks; ++i) {
        tLweCopy(out[i].Get(), lhs[i].Get(), ctx.tlwe_params);
        tLweAddTo(out[i].Get(), rhs[i].Get(), ctx.tlwe_params);
    }
    const auto end = Clock::now();

    g_benchmark_sink ^= static_cast<uint64_t>(out.back().Get()->a[0].coefsT[0]);
    return Milliseconds(end - start);
}

double BenchmarkClientDecrypt(const oram::onion_ring::TFHEContext& ctx,
                              const BenchmarkPlan& plan) {
    auto ciphertexts = MakeRlweArray(plan.block_chunks, ctx);
    TorusPolynomial* plaintext = new_TorusPolynomial(ctx.tlwe_params->N);
    uint64_t checksum = 0;

    const auto start = Clock::now();
    for (size_t i = 0; i < plan.block_chunks; ++i) {
        TLweSymDecryptKaratsuba(plaintext, ciphertexts[i].Get(), ctx.tlwe_key,
                                kPlaintextModulus);
        checksum += static_cast<uint32_t>(plaintext->coefsT[i % ctx.tlwe_params->N]);
    }
    const auto end = Clock::now();

    delete_TorusPolynomial(plaintext);
    g_benchmark_sink ^= checksum;
    return Milliseconds(end - start);
}

double BenchmarkExpansion(const oram::onion_ring::TFHEContext& ctx, const BenchmarkPlan& plan) {
    auto packed = MakeRlweArray(plan.packed_swap_ciphertexts, ctx);
    auto selector = MakeTrivialRgswBit(true, ctx);
    oram::onion_ring::RLWECiphertext substituted(ctx.tlwe_params);
    oram::onion_ring::RLWECiphertext expanded(ctx.tlwe_params);

    const auto start = Clock::now();
    for (size_t packed_idx = 0; packed_idx < plan.packed_swap_ciphertexts; ++packed_idx) {
        for (size_t out_idx = 0; out_idx < plan.expansion_outputs_per_packed_ciphertext;
             ++out_idx) {
            const int32_t exponent =
                static_cast<int32_t>((out_idx * 7 + packed_idx) % (2 * ctx.tlwe_params->N));
            oram::onion_ring::Subs(substituted.Get(), packed[packed_idx].Get(), exponent,
                                   ctx.tlwe_params);
            ExternalProductKaratsuba(expanded.Get(), selector.Get(), substituted.Get(),
                                     ctx.tgsw_params);
        }
    }
    const auto end = Clock::now();

    g_benchmark_sink ^= static_cast<uint64_t>(expanded.Get()->a[0].coefsT[0]);
    return Milliseconds(end - start);
}

double BenchmarkWaksmanCmuxScaled(const oram::onion_ring::TFHEContext& ctx,
                                  const BenchmarkPlan& plan) {
    auto d0 = MakeRlweArray(1, ctx);
    auto d1 = MakeRlweArray(1, ctx);
    auto control = MakeTrivialRgswBit(true, ctx);
    oram::onion_ring::RLWECiphertext out(ctx.tlwe_params);

    const auto start = Clock::now();
    for (size_t i = 0; i < plan.cmux_sample_ops; ++i) {
        CMuxKaratsuba(out.Get(), control.Get(), d1.front().Get(), d0.front().Get(),
                      ctx.tgsw_params);
    }
    const auto end = Clock::now();

    g_benchmark_sink ^= static_cast<uint64_t>(out.Get()->a[0].coefsT[0]);
    const double sampled_ms = Milliseconds(end - start);
    return sampled_ms * static_cast<double>(kWaksmanCmuxOps) /
           static_cast<double>(plan.cmux_sample_ops);
}

void PrintTiming(const std::string& name, double ms) {
    std::cout << std::left << std::setw(18) << name << std::right << std::fixed
              << std::setprecision(3) << ms << " ms\n";
}

BenchmarkPlan ParseArgs(int argc, char** argv) {
    BenchmarkPlan plan;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--quick") {
            plan.block_chunks = 2;
            plan.packed_swap_ciphertexts = 1;
            plan.expansion_outputs_per_packed_ciphertext = 2;
            plan.cmux_sample_ops = 2;
            plan.quick = true;
        }
    }
    return plan;
}

}  // namespace

int main(int argc, char** argv) {
    const BenchmarkPlan plan = ParseArgs(argc, argv);
    const auto cfg = MakeBenchmarkConfig();
    auto ctx = oram::onion_ring::TFHEContext::CreateClientContext(cfg);

    std::cout << "Onion Ring ORAM TFHE macro benchmark\n";
    std::cout << "n=" << cfg.tlwe_n << ", q=2^64 torus, alpha=2^-55, t=2^12"
              << ", RGSW B=2^" << cfg.tgsw_bgbit << ", l=" << cfg.tgsw_l
              << ", RLWE KS B=2^5, l=10\n";
    std::cout << "1 MB block model: " << kBlockChunks << " RLWE chunks of " << kChunkBytes
              << " bytes; Waksman(" << kWaksmanInputs << ") swap gates="
              << kWaksmanSwapGates << "\n\n";
    if (plan.quick) {
        std::cout << "Quick smoke mode: reduced measured loops; Waksman is still scaled to "
                  << kWaksmanCmuxOps << " CMux operations.\n\n";
    }

    PrintTiming("T_server_add", BenchmarkServerAdd(ctx, plan));
    PrintTiming("T_client_decrypt", BenchmarkClientDecrypt(ctx, plan));
    PrintTiming("T_expansion", BenchmarkExpansion(ctx, plan));
    PrintTiming("T_Waksman", BenchmarkWaksmanCmuxScaled(ctx, plan));

    return static_cast<int>(g_benchmark_sink == 0xFFFFFFFFFFFFFFFFULL);
}
