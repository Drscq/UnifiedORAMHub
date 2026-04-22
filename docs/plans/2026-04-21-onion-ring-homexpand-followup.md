# Onion Ring HomExpand Follow-Up

## Current State

The repository now contains:

- real TFHE integration through `FetchContent`,
- RAII wrappers for TLWE and TGSW ciphertexts,
- CMux, external product, and substitution helpers,
- a tested Waksman permutation layer,
- a working Onion Ring client/server protocol over `NetIO`,
- direct TGSW swap-bit transport for triplet eviction and leaf refresh,
- end-to-end eviction and leaf refresh behavior with runtime-configurable parameters,
- a long-run regression that exercises more than 300 eviction windows.

The current protocol now uses the server-side homomorphic permutation flow with
one encrypted TGSW swap-bit ciphertext per gate. What remains is the
paper-faithful packed-swap-bit `HomExpand` path that compresses many swap bits
into a small number of RLWE ciphertexts.

## Follow-Up Goal

Replace the direct-per-gate swap-bit transport with the paper-style
constant-bandwidth server-side permutation flow:

1. Client packs many swap bits into a small number of RLWE ciphertexts.
2. Server expands packed RLWE swap bits into per-gate RGSW ciphertexts.
3. Server evaluates the Waksman network homomorphically in place.
4. Client only receives the leaf-refresh traffic required by the protocol.

## Required Work

### 1. Add packed swap-bit transport

- Extend `PermGen.h/.cpp` from direct TGSW transport to packed RLWE transport.
- Pack up to `N` swap bits per RLWE ciphertext.
- Send packed RLWE ciphertexts instead of direct per-gate TGSW ciphertexts.

### 2. Implement `HomExpand`

- Introduce `HomExpand.h/.cpp`.
- Build the recursive packed-coefficient expansion path described in the CCS 2019 design.
- Add the client-generated expansion keys needed by the server.

### 3. Restore server-side triplet evaluation

- Replace the direct TGSW swap-bit transport in `OnionRingClient::TripletEvict`.
- Route triplet and refresh payloads through packed RLWE ciphertexts plus `HomExpand`.
- Remove the direct-per-gate swap-bit dependency from the steady-state eviction path.

### 4. Tighten verification

- Add a stress/regression test that exercises the packed-swap-bit path.
- Compare the direct TGSW and packed `HomExpand` server-side permutations on the same traced inputs.
- Re-run the Onion Ring end-to-end suite under both debug and release builds.

## Success Criteria

The follow-up is complete when:

- triplet eviction and leaf refresh no longer depend on direct per-gate swap-bit transport,
- the server-side permutation path is covered by automated tests,
- the existing Onion Ring end-to-end tests still pass,
- the packed-swap-bit path is documented and reproducible from the current codebase.
