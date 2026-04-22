# Onion Ring Packed HomExpand Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Replace direct per-gate TGSW swap-bit transport with packed RLWE transport and a bootstrap-free `HomExpand` path that reconstructs the `RGSWCiphertext` controls needed by `WaksmanNetwork::EvalWaksman()`.

**Architecture:** Keep the current client/server Onion Ring protocol and Waksman evaluation boundary intact. Extend `PermGen` to pack swap bits into RLWE ciphertexts, add a one-time expansion bundle for recursive substitution and RLWE key switching, and implement `HomExpand` so the server can recover the same per-gate `RGSWCiphertext` controls that the direct oracle path produces today.

**Tech Stack:** C++17, TFHE C API (`TLweSample`, `TGswSample`, `LweKeySwitchKey`, `tLweExtractLweSample`, `lweKeySwitch`, `tGswExternProduct`), GoogleTest, existing `oram::network::NetIO`.

---

### Task 1: Add failing packed-transport and HomExpand tests

**Files:**
- Create: `test/onion_ring/homexpand_test.cpp`
- Modify: `test/CMakeLists.txt`
- Modify: `test/onion_ring/waksman_test.cpp`

**Step 1: Write the failing tests**

Add tests that define the new public behavior:

```cpp
TEST(PermGenTest, PackedSwapBitPayloadCompressesGateBitsIntoRlweCiphertexts) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    std::vector<size_t> permutation = {5, 2, 7, 1, 6, 0, 4, 3};
    PackedSwapBitPayload payload = BuildPackedSwapBitPayload(permutation, ctx);

    EXPECT_LT(payload.ciphertexts.size(), WaksmanNetwork(permutation.size()).NumGates());
    EXPECT_EQ(payload.bit_count, WaksmanNetwork(permutation.size()).NumGates());
}

TEST(HomExpandTest, PackedPayloadExpandsToSameBitsAsDirectOracle) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);
    PackedSwapBitPayload payload = BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx);

    auto packed_bits = HomExpandPackedSwapBits(payload, bundle, server_ctx);
    auto direct_bits = DeserializeDirectSwapBitPayload(
        BuildDirectSwapBitPayload({2, 0, 3, 1}, client_ctx), server_ctx.tgsw_params);

    ASSERT_EQ(packed_bits.size(), direct_bits.size());
    for (size_t i = 0; i < packed_bits.size(); ++i) {
        EXPECT_EQ(DecryptBit(packed_bits[i], client_ctx), DecryptBit(direct_bits[i], client_ctx));
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'PermGenTest|HomExpandTest'`

Expected:
- compile fails because `PackedSwapBitPayload`, `ExpansionBundle`, or `HomExpandPackedSwapBits` do not exist yet.

**Step 3: Add the new test target source**

Modify `test/CMakeLists.txt` so `onion_ring_tests` includes:

```cmake
add_executable(onion_ring_tests
    onion_ring/tfhe_adapter_test.cpp
    onion_ring/hom_ops_test.cpp
    onion_ring/waksman_test.cpp
    onion_ring/homexpand_test.cpp
    onion_ring/onion_ring_e2e_test.cpp
)
```

**Step 4: Commit**

```bash
git add test/CMakeLists.txt test/onion_ring/waksman_test.cpp test/onion_ring/homexpand_test.cpp
git commit -m "test: add failing packed homexpand coverage"
```

### Task 2: Add expansion-bundle and packed-payload scaffolding

**Files:**
- Modify: `include/oram/onion_ring/TFHEAdapter.h`
- Modify: `src/onion_ring/TFHEAdapter.cpp`
- Modify: `include/oram/onion_ring/PermGen.h`
- Modify: `src/onion_ring/PermGen.cpp`

**Step 1: Write the failing adapter and transport tests**

Add small tests for serialization and round-trip behavior:

```cpp
TEST(TFHEAdapterTest, ExpansionBundleRoundTripsKeySwitchMaterial) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(ctx);
    ExpansionBundle restored =
        ExpansionBundle::Deserialize(bundle.Serialize(), cfg, ctx.tlwe_params, ctx.tgsw_params);

    EXPECT_EQ(restored.subs_keys.size(), bundle.subs_keys.size());
}
```

**Step 2: Run tests to verify they fail**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'TFHEAdapterTest|PermGenTest'`

Expected:
- compile fails because expansion-bundle types and serialization helpers do not exist.

**Step 3: Add minimal scaffolding**

Extend `TFHEAdapter` with RAII for key-switch keys and bundle objects:

```cpp
class LweKeySwitchKeyHandle {
   public:
    explicit LweKeySwitchKeyHandle(LweKeySwitchKey* key = nullptr);
    ~LweKeySwitchKeyHandle();
    LweKeySwitchKeyHandle(LweKeySwitchKeyHandle&& other) noexcept;
    LweKeySwitchKeyHandle& operator=(LweKeySwitchKeyHandle&& other) noexcept;

    LweKeySwitchKey* Get();
    const LweKeySwitchKey* Get() const;
    std::vector<uint8_t> Serialize() const;
    static LweKeySwitchKeyHandle Deserialize(const std::vector<uint8_t>& bytes);
};

struct ExpansionBundle {
    std::vector<RGSWCiphertext> substitution_keys;
    std::vector<LweKeySwitchKeyHandle> lwe_key_switch_keys;
    RGSWCiphertext neg_sk_rgsw;

    std::vector<uint8_t> Serialize() const;
    static ExpansionBundle Deserialize(const std::vector<uint8_t>& bytes,
                                       const RuntimeConfig& config,
                                       const TLweParams* tlwe_params,
                                       const TGswParams* tgsw_params);
};
```

Extend `PermGen` with packed payload types:

```cpp
struct PackedSwapBitPayload {
    uint64_t bit_count = 0;
    std::vector<std::vector<uint8_t>> ciphertexts;
};

PackedSwapBitPayload BuildPackedSwapBitPayload(const std::vector<size_t>& permutation,
                                              const TFHEContext& ctx);
void SendPackedSwapBitPayload(network::NetIO* net_io, const PackedSwapBitPayload& payload);
PackedSwapBitPayload RecvPackedSwapBitPayload(network::NetIO* net_io);
```

For this task, the implementation may be placeholder-but-valid:
- construct bundle containers,
- serialize lengths and byte vectors,
- pack bits into RLWE coefficient slots with `tLweSymEncrypt`,
- do not implement `HomExpand` yet.

**Step 4: Run tests to verify they pass**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'TFHEAdapterTest|PermGenTest'`

Expected:
- serialization tests pass,
- packed transport tests pass,
- `HomExpandTest` still fails because unpacking is not implemented yet.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/TFHEAdapter.h src/onion_ring/TFHEAdapter.cpp include/oram/onion_ring/PermGen.h src/onion_ring/PermGen.cpp test/onion_ring/tfhe_adapter_test.cpp test/onion_ring/waksman_test.cpp
git commit -m "feat: scaffold packed swap payloads and expansion bundle"
```

### Task 3: Add `HomExpand` public API and failing oracle-comparison tests

**Files:**
- Create: `include/oram/onion_ring/HomExpand.h`
- Create: `src/onion_ring/HomExpand.cpp`
- Modify: `CMakeLists.txt`
- Modify: `test/onion_ring/homexpand_test.cpp`

**Step 1: Write the failing `HomExpand` tests**

Expand `homexpand_test.cpp` with focused oracle comparisons:

```cpp
TEST(HomExpandTest, ExpandRlweRecoversOneBitPerGateForSmallPermutation) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);
    PackedSwapBitPayload payload = BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx);

    auto expanded = HomExpandPackedSwapBits(payload, bundle, server_ctx);

    ASSERT_EQ(expanded.size(), WaksmanNetwork(4).NumGates());
}

TEST(HomExpandTest, PackedAndDirectPathsDriveIdenticalWaksmanOutputs) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    std::vector<size_t> permutation = {2, 0, 3, 1};
    auto packed_controls = HomExpandPackedSwapBits(
        BuildPackedSwapBitPayload(permutation, client_ctx), bundle, server_ctx);
    auto direct_controls = DeserializeDirectSwapBitPayload(
        BuildDirectSwapBitPayload(permutation, client_ctx), server_ctx.tgsw_params);

    // Encrypt four blocks and assert the same decrypted output after EvalWaksman().
}
```

**Step 2: Run tests to verify they fail**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R HomExpandTest`

Expected:
- compile or runtime failure because `HomExpandPackedSwapBits()` is not implemented.

**Step 3: Add the new source file to the build**

Modify `CMakeLists.txt`:

```cmake
add_library(onion_ring_core
    src/onion_ring/TFHEAdapter.cpp
    src/onion_ring/HomOps.cpp
    src/onion_ring/WaksmanNetwork.cpp
    src/onion_ring/PermGen.cpp
    src/onion_ring/HomExpand.cpp
    src/onion_ring/OnionRingServer.cpp
    src/onion_ring/OnionRingClient.cpp
)
```

**Step 4: Add the public `HomExpand` API**

Use a narrow interface:

```cpp
std::vector<RGSWCiphertext> HomExpandPackedSwapBits(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx);
```

Keep the first implementation as a stub that throws `std::runtime_error("not implemented")`.

**Step 5: Commit**

```bash
git add CMakeLists.txt include/oram/onion_ring/HomExpand.h src/onion_ring/HomExpand.cpp test/onion_ring/homexpand_test.cpp
git commit -m "test: add homexpand oracle comparison harness"
```

### Task 4: Implement recursive RLWE expansion (`expandRlwe`)

**Files:**
- Modify: `include/oram/onion_ring/HomExpand.h`
- Modify: `src/onion_ring/HomExpand.cpp`
- Modify: `test/onion_ring/homexpand_test.cpp`
- Check: `include/oram/onion_ring/HomOps.h`
- Check: `src/onion_ring/HomOps.cpp`

**Step 1: Add a failing unit test for recursive splitting**

Add a small test that isolates a known bit pattern:

```cpp
TEST(HomExpandTest, RecursiveExpandRlweIsolatesPackedBitsInOrder) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    PackedSwapBitPayload payload = BuildPackedSwapBitPayload({1, 0}, client_ctx);
    auto expanded_rlwe = ExpandPackedRlwe(payload, bundle, server_ctx);

    ASSERT_GE(expanded_rlwe.size(), 1U);
}
```

**Step 2: Run the test to verify it fails**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R RecursiveExpandRlwe`

Expected:
- fail because `ExpandPackedRlwe()` does not exist or returns incorrect results.

**Step 3: Implement minimal recursive expansion**

Add helper functions in `HomExpand.cpp`:

```cpp
static std::vector<RLWECiphertext> ExpandPackedRlwe(const PackedSwapBitPayload& payload,
                                                    const ExpansionBundle& bundle,
                                                    const TFHEContext& server_ctx) {
    std::vector<RLWECiphertext> result;
    for (const auto& bytes : payload.ciphertexts) {
        RLWECiphertext packed = RLWECiphertext::Deserialize(bytes, server_ctx.tlwe_params);
        // Recurse with Subs() and key-switch material until one coefficient remains per sample.
        // Preserve bit ordering to match Waksman gate order.
    }
    return result;
}
```

The minimal implementation should:
- deserialize packed RLWE samples,
- recursively apply `Subs()` to split coefficient sets,
- use the bundle's key-switch material after each substitution,
- truncate the result to `payload.bit_count`.

**Step 4: Run the tests to verify they pass**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'RecursiveExpandRlwe|HomExpandTest.ExpandRlweRecoversOneBitPerGateForSmallPermutation'`

Expected:
- recursive expansion tests pass,
- end-to-end packed-vs-direct tests may still fail because RLWE-to-RGSW lift is not implemented yet.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/HomExpand.h src/onion_ring/HomExpand.cpp test/onion_ring/homexpand_test.cpp
git commit -m "feat: add recursive packed rlwe expansion"
```

### Task 5: Implement RLWE-to-RGSW lift (`homExpand`)

**Files:**
- Modify: `src/onion_ring/HomExpand.cpp`
- Modify: `src/onion_ring/TFHEAdapter.cpp`
- Modify: `test/onion_ring/homexpand_test.cpp`

**Step 1: Add a failing lift test**

Add a test that compares packed and direct controls bit-by-bit:

```cpp
TEST(HomExpandTest, HomExpandLiftsIsolatedRlweBitsIntoEquivalentRgswControls) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    auto packed_controls = HomExpandPackedSwapBits(
        BuildPackedSwapBitPayload({5, 2, 7, 1, 6, 0, 4, 3}, client_ctx),
        bundle,
        server_ctx);

    for (const auto& control : packed_controls) {
        EXPECT_TRUE(DecryptBit(control, client_ctx) == 0 || DecryptBit(control, client_ctx) == 1);
    }
}
```

**Step 2: Run the test to verify it fails**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'HomExpandLifts|PackedAndDirectPathsDriveIdenticalWaksmanOutputs'`

Expected:
- tests fail because the final lift still returns placeholders or wrong controls.

**Step 3: Implement the final lift**

In `HomExpand.cpp`, take isolated RLWE bits and produce `RGSWCiphertext`s:

```cpp
static RGSWCiphertext LiftRlweBitToRgsw(const RLWECiphertext& bit_rlwe,
                                        const ExpansionBundle& bundle,
                                        const TFHEContext& server_ctx) {
    RGSWCiphertext out(server_ctx.tgsw_params);
    // Use the paper-style external-product path with the support material in bundle.neg_sk_rgsw.
    // The result must decrypt under the client key to the same bit as the direct oracle path.
    return out;
}
```

The implementation should:
- consume the isolated RLWE bit ciphertext,
- use the bundle's `RGSW(-s)` support material,
- produce one `RGSWCiphertext` per gate control,
- preserve control ordering.

**Step 4: Run tests to verify they pass**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R HomExpandTest`

Expected:
- all `HomExpandTest` cases pass.

**Step 5: Commit**

```bash
git add src/onion_ring/HomExpand.cpp src/onion_ring/TFHEAdapter.cpp test/onion_ring/homexpand_test.cpp
git commit -m "feat: add rlwe to rgsw homexpand lift"
```

### Task 6: Route Onion Ring protocol handlers through packed transport

**Files:**
- Modify: `include/oram/onion_ring/OnionRingClient.h`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `include/oram/onion_ring/OnionRingServer.h`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `src/onion_ring/PermGen.cpp`

**Step 1: Add a failing protocol regression test**

Extend end-to-end coverage so the live protocol uses the packed path:

```cpp
TEST_F(OnionRingE2ETest, PackedSwapBitTransportPreservesBlocksAcrossEvictions) {
    StartServer(EvictingConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    // Write several blocks, trigger evictions, and verify reads still match.
}
```

**Step 2: Run the test to verify it fails**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'PackedSwapBitTransportPreservesBlocksAcrossEvictions|OnionRingE2ETest.MoreThanThreeHundredEvictionWindowsPreserveRewrittenBlocks'`

Expected:
- failure because the live client/server path still uses direct transport.

**Step 3: Replace the live transport**

Update `OnionRingClient`:

```cpp
void OnionRingClient::TripletEvict(size_t source_idx, size_t left_idx, size_t right_idx) {
    // Build permutations.
    // Build packed payloads with BuildPackedSwapBitPayload().
    // Send packed payloads instead of SendDirectSwapBitPayload().
}
```

Update `OnionRingServer`:

```cpp
void OnionRingServer::HandleEvictTriplet() {
    // Receive packed payloads.
    // Expand with HomExpandPackedSwapBits().
    // Reuse WaksmanNetwork::EvalWaksman().
}
```

Also add a one-time initialization exchange so the server receives and caches `ExpansionBundle`.

**Step 4: Run the tests to verify they pass**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'OnionRingE2ETest|PermGenTest|HomExpandTest'`

Expected:
- packed-path protocol tests pass,
- long-horizon eviction test passes on the live protocol.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/OnionRingClient.h src/onion_ring/OnionRingClient.cpp include/oram/onion_ring/OnionRingServer.h src/onion_ring/OnionRingServer.cpp src/onion_ring/PermGen.cpp test/onion_ring/onion_ring_e2e_test.cpp
git commit -m "feat: route onion ring protocol through packed homexpand path"
```

### Task 7: Remove direct transport from the live path and tighten regression coverage

**Files:**
- Modify: `include/oram/onion_ring/PermGen.h`
- Modify: `src/onion_ring/PermGen.cpp`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `test/onion_ring/waksman_test.cpp`
- Modify: `test/onion_ring/homexpand_test.cpp`
- Modify: `docs/plans/2026-04-21-onion-ring-homexpand-followup.md`
- Modify: `README.md`

**Step 1: Add a failing cleanup/regression test**

Add a regression that proves the packed and direct oracles still agree:

```cpp
TEST(HomExpandTest, PackedPathMatchesDirectOracleAcrossMultiplePermutations) {
    // Iterate over several permutations and compare decrypted gate controls and Waksman outputs.
}
```

**Step 2: Run the test to verify it fails**

Run: `cd worktrees/onion-ring-oram && cmake --build build --target onion_ring_tests -j"$(nproc)" && ctest --test-dir build --output-on-failure -R 'PackedPathMatchesDirectOracle|HomExpandTest'`

Expected:
- failure because comparison coverage is incomplete or remaining direct/live code paths diverge.

**Step 3: Clean up live protocol and docs**

- keep direct transport helpers only if tests still need them as an oracle,
- remove any direct-transport use from production handlers,
- update the follow-up doc to mark packed transport complete,
- update `README.md` to describe packed RLWE swap-bit transport as the live protocol.

**Step 4: Run focused tests to verify they pass**

Run: `cd worktrees/onion-ring-oram && ctest --test-dir build --output-on-failure -R 'WaksmanTest|PermGenTest|HomExpandTest|OnionRingE2ETest|OnionRingScheduleTest'`

Expected:
- all focused Onion Ring tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/PermGen.h src/onion_ring/PermGen.cpp src/onion_ring/OnionRingClient.cpp src/onion_ring/OnionRingServer.cpp test/onion_ring/waksman_test.cpp test/onion_ring/homexpand_test.cpp docs/plans/2026-04-21-onion-ring-homexpand-followup.md README.md
git commit -m "refactor: finalize packed homexpand onion ring path"
```

### Task 8: Run full verification and prepare branch completion

**Files:**
- Check: `test/onion_ring/*.cpp`
- Check: `README.md`
- Check: `docs/plans/2026-04-21-onion-ring-homexpand-followup.md`

**Step 1: Run the full Onion Ring suite**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R 'TFHEAdapterTest|HomOpsTest|WaksmanTest|PermGenTest|HomExpandTest|OnionRingE2ETest|OnionRingScheduleTest'
```

Expected:
- all Onion Ring tests pass.

**Step 2: Run the full repository suite**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure
```

Expected:
- all repository tests pass.

**Step 3: Verify documentation**

Check that:
- `README.md` documents packed transport as the live protocol,
- `docs/plans/2026-04-21-onion-ring-homexpand-followup.md` reflects completion or the exact remaining gap,
- the long-horizon stress test is still documented as part of manual validation.

**Step 4: Commit**

```bash
git add README.md docs/plans/2026-04-21-onion-ring-homexpand-followup.md
git commit -m "test: verify packed homexpand onion ring end to end"
```
