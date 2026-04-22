# Onion Ring Packed Transport and HomExpand Design

## Goal

Finish the Onion Ring ORAM follow-up by replacing direct per-gate TGSW swap-bit transport
with packed RLWE transport and a server-side `HomExpand` path that reconstructs the
per-gate control ciphertexts needed by `WaksmanNetwork::EvalWaksman()`.

## Current Baseline

The branch already has:

- real TFHE integration via `FetchContent`,
- runtime-configurable Onion Ring parameters,
- `TFHEAdapter` wrappers for TLWE and TGSW ciphertexts,
- tested `CMux`, external product, and substitution helpers,
- a working server-side Waksman permutation pipeline,
- direct TGSW swap-bit transport for triplet eviction and leaf refresh,
- end-to-end Onion Ring tests, including a long-run stress test with more than `300 * a`
  client accesses.

The remaining gap is communication cost. The current protocol still sends one serialized
`RGSWCiphertext` per swap gate. The optimization phase should compress many swap bits into a
small number of `RLWECiphertext`s while keeping the same Waksman evaluation boundary.

## Design Choice

Use the Option 1 architecture that is already validated by the current code layout:

- the client packs swap bits into RLWE ciphertexts,
- the server expands packed RLWE ciphertexts into per-gate controls,
- the server continues to call `WaksmanNetwork::EvalWaksman()` with a vector of
  `RGSWCiphertext`s,
- access, eviction scheduling, and bucket semantics remain unchanged.

The unpacking path must be bootstrap-free.

This design will follow the paper's `expandRlwe` and `homExpand` structure:

- recursive RLWE expansion using substitution and RLWE key switching,
- external-product-based lifting from isolated RLWE bits into final `RGSWCiphertext`s.

It will not introduce a bootstrapping-based coefficient lift.

## Architecture

### 1. Transport Boundary Stays Stable

`OnionRingClient` and `OnionRingServer` should keep the same high-level responsibilities:

- `OnionRingClient::TripletEvict()` computes the target permutations for the left and right
  child buckets and sends encrypted control information.
- `OnionRingClient::LeafRefresh()` receives the leaf bucket, re-randomizes ciphertexts,
  computes a new permutation, and sends encrypted control information back.
- `OnionRingServer::HandleEvictTriplet()` and `HandleLeafRefresh()` reconstruct the per-gate
  controls and then reuse the existing Waksman evaluator.

The direct transport format changes, but the Waksman evaluation call site should remain the
same. This isolates the optimization to transport and unpacking.

### 2. `PermGen` Becomes a Packed-Transport Layer

`PermGen` should stop being a thin wrapper around direct `RGSWCiphertext` serialization and
instead provide:

- packing of swap bits into RLWE coefficient slots,
- serialization/deserialization of packed RLWE payloads,
- serialization/deserialization of the one-time expansion bundle,
- compatibility helpers for tests that compare packed transport against the current direct
  oracle path.

Each packed RLWE ciphertext should encode up to `N` swap bits, where `N` is the TLWE
polynomial dimension from the runtime configuration.

### 3. `HomExpand` Owns Server-Side Unpacking

Introduce `HomExpand.h/.cpp` as the unpacking layer that converts packed RLWE swap-bit
payloads into `std::vector<RGSWCiphertext>`.

In this design, `HomExpand` has two conceptual phases:

1. `expandRlwe`: recursively split one packed RLWE ciphertext into many RLWE ciphertexts,
   each isolating a single coefficient/bit.
2. `homExpand`: lift the isolated RLWE encryptions into the `RGSWCiphertext` controls that
   `EvalWaksman()` already expects.

The server should never decrypt. `HomExpand` must operate only on ciphertexts and server-side
expansion material received from the client during setup.

## Cryptographic Data Flow

### Client Setup Bundle

The client should generate and send a one-time expansion bundle during initialization. The
bundle needs enough data for the server to run recursive substitution and RLWE-to-RGSW lift
without secret keys.

The bundle should contain:

- substitution support keys for each recursive expansion level,
- RLWE key-switch material that returns substituted ciphertexts to the canonical working key,
- the support material for the final RLWE-to-RGSW lift,
- serialization metadata so the server can reconstruct the bundle with the active runtime
  parameters.

The paper-specific detail that should be preserved here is the final lift material:

- the server needs access to an `RGSWCiphertext` encryption of `-s`,
- this object is used in the external-product-based lift described by the paper's
  `homExpand` algorithm.

### Access-Time Packed Payloads

For each triplet eviction or leaf refresh:

- the client derives the target permutation,
- `PermGen` computes the swap bits using the existing `WaksmanNetwork`,
- `PermGen` packs those bits into one or more RLWE ciphertexts,
- the client sends the packed RLWE payloads instead of one direct TGSW ciphertext per gate.

### Server-Side Expansion

For each packed payload:

- the server deserializes the RLWE ciphertexts,
- `HomExpand` recursively applies `Subs` and RLWE key switching to isolate the coefficients,
- `HomExpand` lifts the isolated RLWE bits into `RGSWCiphertext`s using external products and
  the support material from setup,
- the resulting `RGSWCiphertext` vector is passed into `WaksmanNetwork::EvalWaksman()`.

This preserves the current Waksman boundary and minimizes protocol churn.

## Components

### `TFHEAdapter`

`TFHEAdapter` will need additional support for:

- serializing/deserializing RLWE key-switch keys,
- constructing and holding extra expansion-key material in `TFHEContext` or a closely related
  structure,
- helper RAII wrappers if the expansion bundle introduces new TFHE object lifetimes.

### `PermGen`

`PermGen` will need:

- a packed swap-bit payload type,
- client-side bit-packing into RLWE coefficient slots,
- send/receive helpers for packed payloads,
- send/receive helpers for the one-time expansion bundle,
- compatibility with the existing direct payload helper so tests can compare both paths.

### `HomExpand`

`HomExpand` will need:

- recursive expansion logic modeled after `expandRlwe`,
- server-side unpacking entry points that accept packed RLWE payloads plus expansion material,
- final RLWE-to-RGSW lift logic modeled after `homExpand`,
- targeted regression tests against the direct transport oracle.

### `OnionRingClient` and `OnionRingServer`

Protocol updates should be minimal:

- initialization needs a setup phase for the one-time expansion bundle,
- triplet eviction and leaf refresh use packed transport,
- server handlers call `HomExpand` before `EvalWaksman()`,
- the direct transport path should remain only as a test oracle until the packed path is
  validated, then be removed from the live protocol.

## Error Handling

The packed path should fail loudly when:

- packed payload sizes do not match the runtime configuration,
- setup bundle contents are missing or malformed,
- the unpacked gate-control count does not match the Waksman network size,
- recursive expansion attempts to use unsupported dimensions or invalid substitution indices.

Runtime errors should preserve the existing fail-fast server behavior rather than silently
falling back to direct transport in production code.

## Testing Strategy

### 1. `PermGen` Tests

Add tests that verify:

- swap bits are packed into the expected number of RLWE ciphertexts,
- packed payload serialization is round-trippable,
- packed and direct payloads correspond to the same plaintext swap-bit sequence.

### 2. `HomExpand` Tests

Add tests that verify:

- a packed RLWE payload expands to the same logical swap-bit sequence as the direct path,
- the RLWE-to-RGSW lift produces controls that select the same branches as the direct TGSW
  oracle,
- recursive expansion works for small synthetic examples before being used on full bucket
  permutations.

### 3. Waksman Comparison Tests

Add regression tests that:

- feed the same permutation through the direct path and the packed path,
- evaluate both with `WaksmanNetwork::EvalWaksman()`,
- confirm identical decrypted outputs.

### 4. End-to-End Tests

Re-run the current Onion Ring end-to-end suite with the packed path as the live protocol,
including:

- read/write correctness,
- triplet eviction correctness,
- leaf refresh correctness,
- the existing `> 300 * a` long-horizon regression.

## Success Criteria

This follow-up is complete when:

- the live protocol no longer sends one `RGSWCiphertext` per swap gate,
- `PermGen` sends packed RLWE payloads plus one-time expansion material,
- `HomExpand` reconstructs the exact `RGSWCiphertext` controls needed by
  `WaksmanNetwork::EvalWaksman()`,
- direct transport remains only as a test oracle or is removed entirely,
- the full Onion Ring and repository test suites still pass.
