# UnifiedORAMHub

UnifiedORAMHub is a C++17 workspace for building and comparing ORAM protocols.
This branch contains two concrete implementations:

- Path ORAM over the existing `oram_core` client/server stack
- Onion Ring ORAM under `oram::onion_ring`, backed by real TFHE primitives and real TCP networking

## Onion Ring Status

The Onion Ring implementation on this branch includes:

- TFHE integrated through CMake `FetchContent` from `tfhe/tfhe` `v1.0.1`
- RAII wrappers for TLWE and TGSW ciphertexts
- CMux, external product, and substitution helpers
- a tested Waksman permutation layer
- live client/server triplet eviction and leaf refresh over `NetIO`
- a practical packed swap-control transport for eviction and refresh:
  - the client batches encrypted swap controls into fewer transport frames,
  - the server reconstructs the per-gate `RGSWCiphertext` controls used by `EvalWaksman()`,
  - the live protocol no longer sends one swap-control frame per gate
- expansion-bundle scaffolding for the heavier follow-up work:
  - serialized `RGSW(-s)` support material,
  - serialized LWE key-switch material,
  - a `HomExpand` entry point used by the packed live path

The remaining optimization follow-up is the paper-faithful recursive RLWE coefficient-expansion
path from the Onion Ring paper. That follow-up is tracked in
[docs/plans/2026-04-21-onion-ring-homexpand-followup.md](docs/plans/2026-04-21-onion-ring-homexpand-followup.md).

## Repository Layout

- `include/oram/`: public headers
- `src/`: implementation files
- `test/`: GoogleTest coverage
- `docs/plans/`: design notes and follow-up documents

Onion Ring files live under:

- `include/oram/onion_ring/`
- `src/onion_ring/`
- `test/onion_ring/`

## Build

Configure and build from the Onion Ring worktree:

```bash
cd worktrees/onion-ring-oram
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

Notes:

- TFHE is fetched and built automatically during CMake configure.
- The build prefers `tfhe-spqlios-fma` when available, then falls back to `tfhe-spqlios-avx`, then `tfhe-nayuki-portable`.
- Debug builds enable address and undefined-behavior sanitizers for this repository's code.

## Test

Run the full repository test suite:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure
```

Run only the Onion Ring suite:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R 'WaksmanTest|PermGenTest|HomExpandTest|OnionRingE2ETest|OnionRingScheduleTest|TFHEAdapterTest|HomOpsTest'
```

Run the long-horizon Onion Ring regression by itself:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R OnionRingE2ETest.MoreThanThreeHundredEvictionWindowsPreserveRewrittenBlocks
```

That stress test performs more than `300 * a` client accesses and verifies that repeatedly
rewritten blocks are still readable after the resulting eviction and leaf-refresh cycles.

## Manual Validation

There is not yet a standalone Onion Ring demo binary. The intended manual validation path on
this branch is:

1. Build the repository.
2. Run the focused Onion Ring suite.
3. Run the long-horizon end-to-end regression above.
4. Inspect the implementation in:
   - [OnionRingClient.cpp](src/onion_ring/OnionRingClient.cpp)
   - [OnionRingServer.cpp](src/onion_ring/OnionRingServer.cpp)
   - [PermGen.cpp](src/onion_ring/PermGen.cpp)
   - [HomExpand.cpp](src/onion_ring/HomExpand.cpp)

If you want a dedicated CLI driver for starting an Onion Ring server and client outside the test
harness, that would be a small follow-up on top of the current library code.

## Key Targets

- `oram_core`: shared networking, crypto, and Path ORAM support
- `onion_ring_core`: Onion Ring implementation plus TFHE-backed homomorphic operations
- `oram_tests`: core, crypto, networking, and Path ORAM tests
- `onion_ring_tests`: TFHE, homomorphic ops, Waksman, and Onion Ring end-to-end tests

## License

See [LICENSE](LICENSE).
