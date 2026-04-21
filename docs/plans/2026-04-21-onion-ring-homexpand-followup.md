# Onion Ring HomExpand Follow-Up

## Current State

The repository now contains:

- real TFHE integration through `FetchContent`,
- RAII wrappers for TLWE and TGSW ciphertexts,
- CMux, external product, and substitution helpers,
- a tested Waksman permutation layer,
- a working Onion Ring client/server protocol over `NetIO`,
- end-to-end eviction and leaf refresh behavior with runtime-configurable parameters.

The current protocol keeps the cryptographic foundations in place, but uses a
client-assisted bucket rebuild during triplet eviction and leaf refresh rather
than the packed-swap-bit `HomExpand` path from the paper.

## Follow-Up Goal

Replace the client-assisted triplet/refresh rebuild with the paper-style
constant-bandwidth server-side permutation flow:

1. Client packs many swap bits into a small number of RLWE ciphertexts.
2. Server expands packed RLWE swap bits into per-gate RGSW ciphertexts.
3. Server evaluates the Waksman network homomorphically in place.
4. Client only receives the leaf-refresh traffic required by the protocol.

## Required Work

### 1. Add packed swap-bit transport

- Introduce `PermGen.h/.cpp`.
- Pack up to `N` swap bits per RLWE ciphertext.
- Send packed RLWE ciphertexts instead of direct per-gate TGSW ciphertexts.

### 2. Implement `HomExpand`

- Introduce `HomExpand.h/.cpp`.
- Build the recursive packed-coefficient expansion path described in the CCS 2019 design.
- Add the client-generated expansion keys needed by the server.

### 3. Restore server-side triplet evaluation

- Replace the client-assisted bucket rebuild in `OnionRingClient::TripletEvict`.
- Route triplet and refresh payloads back through the server-side permutation handlers.
- Remove the bucket-read dependency from the steady-state eviction path.

### 4. Tighten verification

- Add a disabled stress/regression test that exercises the packed-swap-bit path.
- Compare the client-assisted and homomorphic server-side permutations on the same traced inputs.
- Re-run the Onion Ring end-to-end suite under both debug and release builds.

## Success Criteria

The follow-up is complete when:

- triplet eviction and leaf refresh no longer depend on client-side bucket fetches,
- the server-side permutation path is covered by automated tests,
- the existing Onion Ring end-to-end tests still pass,
- the packed-swap-bit path is documented and reproducible from the current codebase.
