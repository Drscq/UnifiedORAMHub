# Onion Ring ORAM Hybrid Prototype Design

**Date:** 2026-04-20

## Goal

Build a practical Onion Ring ORAM prototype in `UnifiedORAMHub` that uses:

- the real `tfhe/tfhe` `v1.0.1` library for leveled TLWE/TGSW operations,
- the existing TCP-based `oram::network::NetIO` transport,
- the existing Path ORAM client/server structure as the integration template,
- a staged delivery plan:
  - foundation-first cryptographic and networking work,
  - then a working Onion Ring client/server protocol,
  - while deferring `HomExpand` and packed swap-bit transport.

## Approved Direction

The approved implementation path is a hybrid:

1. Land TFHE integration, adapters, homomorphic operations, and Waksman evaluation first.
2. Build the Onion Ring client/server protocol on top of that foundation.
3. Skip `HomExpand` in the first cut and send eviction swap bits directly as individual TGSW ciphertexts.
4. Keep packed RLWE swap bits and `HomExpand` as a later optimization phase.

This keeps the first end-to-end prototype aligned with the paper's core protocol while avoiding the largest unsupported cryptographic engineering task in vanilla TFHE.

## Existing Repo Context

The current repository gives us a good skeleton but not much cryptographic infrastructure yet:

- `oram_core` already contains:
  - network transport,
  - AES-CTR,
  - core block and bucket types,
  - Path ORAM client/server code.
- `NetIO` already provides raw send/receive primitives but lacks length-prefixed helpers for large ciphertext payloads.
- Tests currently live in a single `oram_tests` target.

The Onion Ring work should follow the same code layout conventions:

- headers under `include/oram/onion_ring/`
- sources under `src/onion_ring/`
- tests under `test/onion_ring/`
- namespace `oram::onion_ring`

## Key Design Corrections

The original draft plan is directionally strong, but a few details need to change for a robust first implementation.

### 1. Use TFHE stream IO, not raw struct serialization

`TLweSample` and `TGswSample` contain internal pointers, so raw memory dumping is not a safe wire format.

We will serialize using TFHE's supported stream helpers:

- `export_tlweSample_toStream`
- `import_tlweSample_fromStream`
- `export_tgswSample_toStream`
- `import_tgswSample_fromStream`

This keeps network payloads compatible with the actual library layout and avoids undefined behavior.

### 2. `HomExpand` is out of scope for phase one

Vanilla TFHE gives us the primitives needed for:

- TLWE encryption/decryption,
- TGSW encryption/decryption,
- external products,
- TLWE arithmetic,
- sample extraction,
- polynomial automorphisms via low-level operations.

It does not give us a clean, ready-made server-side "packed RLWE coefficients to per-gate TGSW ciphertexts" path for Onion Ring. The first cut will therefore send swap bits as direct TGSW ciphertexts.

### 3. Onion Ring must use runtime configuration, not only compile-time constants

The original draft suggested a static `Config` with inheritance-based overrides in tests. That will not work reliably, because production code referencing `Config::kZ` will not observe child overrides.

Onion Ring should instead use a runtime `Parameters` or `RuntimeConfig` object that is passed into:

- TFHE context construction,
- bucket sizing,
- client/server initialization,
- tests with small values such as `Z=4`, `A=3`, `tree_height=3`.

Paper defaults will still be available as default values.

### 4. `onion_ring_core` should remain a separate target

`oram_core` already bundles Path ORAM and general utilities. We should add a separate `onion_ring_core` target that links against:

- `oram_core`
- one TFHE backend library selected by availability
- `OpenSSL::Crypto`

This prevents the existing Path ORAM build from being tightly coupled to TFHE internals.

### 5. The first prototype should use direct TGSW swap-bit transport

During triplet eviction and leaf refresh, the client will compute Waksman swap bits and send them as individual TGSW ciphertexts over `NetIO`.

That is much heavier than packed RLWE transport, but it is functionally equivalent for `EvalWaksman` and allows us to validate:

- the protocol,
- the controlled-swap logic,
- the additive-noise behavior of repeated `CMux`,
- the client/server orchestration.

## Architecture

### Build System

Top-level CMake will:

- fetch `tfhe/tfhe` from GitHub at tag `v1.0.1` using `FetchContent`,
- build TFHE from `src/`,
- disable TFHE's own tests,
- add `onion_ring_core`,
- choose the fastest available backend in this order:
  - `tfhe-spqlios-fma`
  - `tfhe-spqlios-avx`
  - `tfhe-nayuki-portable`

Onion Ring tests will live in a dedicated `onion_ring_tests` binary so that TFHE-dependent tests stay separate from the current lightweight suite.

### Data Model

Each Onion Ring bucket stores `2Z` slots.

Each slot contains:

- a TLWE ciphertext holding one encoded block payload,
- an AES freshness layer for leaf refresh metadata.

For the prototype, a block payload is encoded as a TLWE plaintext polynomial:

- the first `block_size` coefficients encode byte values,
- remaining coefficients are zero,
- `block_size` must stay `<= N` for the active TFHE parameters.

With the default prototype parameters, `N=1024` and `block_size=256`, so this fits.

### Cryptographic Layer

`TFHEAdapter` will provide:

- RAII wrappers for `TLweParams`, `TGswParams`, `TLweKey`, `TGswKey`,
- move-only wrappers for `TLweSample` and `TGswSample`,
- helpers to encode/decode block payloads to/from `TorusPolynomial`,
- stream-based serialization/deserialization.

`HomOps` will provide:

- `ExternalProduct`
- `CMux`
- `Subs`

`CMux` is the critical primitive because Onion Ring's Waksman evaluation depends on additive noise growth rather than multiplicative depth blow-up.

### Waksman Layer

`WaksmanNetwork` will:

- generate swap bits on the client from a target permutation,
- expose a deterministic gate list,
- evaluate the network on the server using `CMux` on encrypted swap bits.

The phase-one transport format for those swap bits is:

- one `TGswSample` per gate,
- length-prefixed vectors over `NetIO`.

### Onion Ring Protocol Layer

`OnionRingClient` and `OnionRingServer` will mirror the existing Path ORAM split:

- separate processes,
- TCP communication through `NetIO`,
- client holds keys, position map, and local metadata,
- server holds encrypted buckets and evaluates homomorphic routing.

Phase-one protocol support will include:

- access:
  - client chooses one slot per bucket on the path,
  - server sums the chosen TLWE ciphertexts,
  - client decrypts, updates, re-encrypts, and buffers at root,
- triplet eviction:
  - client computes destination grouping and permutation,
  - client sends direct TGSW swap bits,
  - server pads, evaluates Waksman permutations, and writes child buckets,
- leaf refresh:
  - server sends leaf bucket TLWE ciphertexts,
  - client decrypts and re-encrypts them freshly,
  - client sends refreshed TLWE ciphertexts plus direct TGSW swap bits,
  - server replaces and permutes the leaf bucket.

## File Plan

### New Onion Ring Headers

- `include/oram/onion_ring/Config.h`
- `include/oram/onion_ring/TFHEAdapter.h`
- `include/oram/onion_ring/HomOps.h`
- `include/oram/onion_ring/OnionBucket.h`
- `include/oram/onion_ring/WaksmanNetwork.h`
- `include/oram/onion_ring/OnionRingClient.h`
- `include/oram/onion_ring/OnionRingServer.h`

### New Onion Ring Sources

- `src/onion_ring/TFHEAdapter.cpp`
- `src/onion_ring/HomOps.cpp`
- `src/onion_ring/WaksmanNetwork.cpp`
- `src/onion_ring/OnionRingClient.cpp`
- `src/onion_ring/OnionRingServer.cpp`

### Modified Existing Files

- `CMakeLists.txt`
- `test/CMakeLists.txt`
- `include/oram/network/NetIO.h`
- `src/network/NetIO.cpp`

### New Tests

- `test/onion_ring/tfhe_adapter_test.cpp`
- `test/onion_ring/hom_ops_test.cpp`
- `test/onion_ring/waksman_test.cpp`
- `test/onion_ring/onion_ring_e2e_test.cpp`

## Testing Strategy

### Unit Tests

1. TFHE adapter tests:
   - TLWE roundtrip
   - TGSW roundtrip
   - TLWE/TGSW serialization roundtrip
   - block payload encode/decode
2. Homomorphic operation tests:
   - external product behavior
   - `CMux` correctness for encrypted `0` and `1`
   - substitution behavior
3. Waksman tests:
   - plaintext gate generation for small network sizes
   - encrypted controlled-swap evaluation for small arrays

### Integration Tests

1. Access-only client/server smoke test:
   - write, read, overwrite
2. Eviction test with small parameters:
   - repeated accesses trigger triplet eviction
   - blocks remain readable afterward
3. Leaf refresh test:
   - refreshed leaf contents remain accessible

All end-to-end tests will use small parameters and localhost TCP.

## Out of Scope for Phase One

- packed RLWE swap-bit transport,
- `HomExpand`,
- RLWE key switching beyond what standard TFHE already provides,
- paper-scale performance at `Z=254`,
- production key exchange or hardened deployment assumptions.

## Phase-Two Follow-Up

Once the prototype is correct and stable, phase two can add:

- packed RLWE permutation-bit transport,
- `HomExpand`,
- communication-size benchmarks comparing direct TGSW vs packed transport,
- larger-parameter experiments approaching the paper defaults.
