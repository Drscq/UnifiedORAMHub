# Onion Ring Recursive HomExpand Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Add the paper-faithful recursive RLWE coefficient-expansion path for Onion Ring while
keeping the current practical packed path as a verified oracle and fallback during development.

**Architecture:** Introduce a dual-mode packed-control pipeline: practical packed controls remain
the trusted baseline, and a new recursive packed RLWE path is added beside it. `HomExpand`
dispatches by mode so the recursive path can be built and validated against the practical oracle
before any default cutover.

**Tech Stack:** C++17, TFHE C API (`TLweSample`, `TGswSample`, `LweKeySwitchKey`,
`tLweExtractLweSampleIndex`, `lweKeySwitch`, `tGswExternProduct`), GoogleTest, existing
`oram::network::NetIO`.

---

### Task 1: Add dual-mode recursive-path tests and transport scaffolding

**Files:**
- Modify: `include/oram/onion_ring/PermGen.h`
- Modify: `src/onion_ring/PermGen.cpp`
- Modify: `include/oram/onion_ring/HomExpand.h`
- Modify: `test/onion_ring/homexpand_test.cpp`
- Modify: `test/onion_ring/waksman_test.cpp`

**Step 1: Write the failing recursive-mode tests**

Add tests that define the recursive-path surface without touching the practical path:

```cpp
TEST(PermGenTest, RecursivePackedPayloadUsesFewerCiphertextsThanGateCount) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    PackedSwapBitPayload payload =
        BuildRecursivePackedSwapBitPayload({5, 2, 7, 1, 6, 0, 4, 3}, ctx);

    EXPECT_LT(payload.ciphertexts.size(), WaksmanNetwork(8).NumGates());
}

TEST(HomExpandTest, RecursiveModeMatchesPracticalOracleBitsForSmallPermutation) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    auto recursive_controls = HomExpandPackedSwapBits(
        BuildRecursivePackedSwapBitPayload({2, 0, 3, 1}, client_ctx), bundle, server_ctx);
    auto practical_controls = HomExpandPackedSwapBits(
        BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx), bundle, server_ctx);

    ASSERT_EQ(recursive_controls.size(), practical_controls.size());
    for (size_t i = 0; i < recursive_controls.size(); ++i) {
        EXPECT_EQ(DecryptBit(recursive_controls[i], client_ctx),
                  DecryptBit(practical_controls[i], client_ctx));
    }
}
```

**Step 2: Run the tests to verify they fail**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'PermGenTest|HomExpandTest'
```

Expected:
- compile or runtime failure because the recursive payload constructor and recursive dispatch
  do not exist yet.

**Step 3: Add transport-mode scaffolding**

Add a mode discriminator that can represent:

- practical packed batching,
- recursive RLWE coefficient packing.

Extend `PackedSwapBitPayload` with the metadata needed for dispatch and recursive decoding, but
do not implement the recursive math yet.

**Step 4: Run the tests to verify the scaffolding compiles**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'PermGenTest|HomExpandTest'
```

Expected:
- practical packed tests still pass,
- recursive tests still fail because unpacking is not implemented.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/PermGen.h src/onion_ring/PermGen.cpp include/oram/onion_ring/HomExpand.h test/onion_ring/homexpand_test.cpp test/onion_ring/waksman_test.cpp
git commit -m "test: add recursive homexpand transport scaffolding"
```

### Task 2: Make the expansion bundle real for recursive substitution

**Files:**
- Modify: `include/oram/onion_ring/TFHEAdapter.h`
- Modify: `src/onion_ring/TFHEAdapter.cpp`
- Modify: `test/onion_ring/tfhe_adapter_test.cpp`

**Step 1: Write the failing bundle tests**

Add tests that require non-placeholder recursive support material:

```cpp
TEST(TFHEAdapterTest, ExpansionBundleContainsRecursiveSupportMaterial) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(ctx);

    EXPECT_FALSE(bundle.substitution_keys.empty());
    EXPECT_FALSE(bundle.lwe_key_switch_keys.empty());
    EXPECT_FALSE(bundle.neg_sk_rgsw_bytes.empty());
}
```

**Step 2: Run the tests to verify they fail or expose placeholders**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R TFHEAdapterTest
```

Expected:
- failure or insufficient assertions until the bundle is upgraded for recursive mode.

**Step 3: Implement the real recursive bundle contents**

Make `BuildExpansionBundle(...)` construct the actual support material required by the recursive
path:

- substitution support keys per recursive level,
- real key-switch material used after substitution,
- serialized `RGSW(-s)` support material.

Keep existing serialization and round-trip support intact.

**Step 4: Run the tests to verify they pass**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R TFHEAdapterTest
```

Expected:
- adapter tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/TFHEAdapter.h src/onion_ring/TFHEAdapter.cpp test/onion_ring/tfhe_adapter_test.cpp
git commit -m "feat: add recursive homexpand support bundle"
```

### Task 3: Implement recursive RLWE coefficient isolation

**Files:**
- Modify: `src/onion_ring/HomExpand.cpp`
- Modify: `test/onion_ring/homexpand_test.cpp`
- Check: `include/oram/onion_ring/HomOps.h`
- Check: `src/onion_ring/HomOps.cpp`

**Step 1: Write the failing recursive-isolation tests**

Add a focused test for gate-order-preserving RLWE isolation:

```cpp
TEST(HomExpandTest, RecursiveExpandRlweIsolatesGateBitsInOrder) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    auto payload = BuildRecursivePackedSwapBitPayload({1, 0}, client_ctx);
    auto isolated = ExpandPackedRlweForTest(payload, bundle, server_ctx);

    ASSERT_GE(isolated.size(), 1U);
}
```

**Step 2: Run the tests to verify they fail**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'RecursiveExpandRlwe|HomExpandTest'
```

Expected:
- failure because recursive RLWE isolation is not implemented.

**Step 3: Implement minimal recursive expansion**

Add internal helpers that:

- deserialize recursive packed RLWE ciphertexts,
- recursively apply `Subs`,
- apply key-switch support after substitution,
- return isolated RLWE ciphertexts in gate order.

Keep the helper internal unless tests need a narrow friend/test-only hook.

**Step 4: Run the tests to verify recursive isolation works**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'RecursiveExpandRlwe|HomExpandTest.ExpandRlweRecoversOneBitPerGateForSmallPermutation'
```

Expected:
- recursive-isolation tests pass,
- final packed-vs-practical semantic comparisons may still fail until lift is implemented.

**Step 5: Commit**

```bash
git add src/onion_ring/HomExpand.cpp test/onion_ring/homexpand_test.cpp
git commit -m "feat: add recursive rlwe coefficient expansion"
```

### Task 4: Implement the paper-faithful RLWE-to-RGSW lift

**Files:**
- Modify: `src/onion_ring/HomExpand.cpp`
- Modify: `test/onion_ring/homexpand_test.cpp`

**Step 1: Write the failing lift comparison tests**

Add tests that compare recursive and practical controls at the Waksman boundary:

```cpp
TEST(HomExpandTest, RecursiveLiftMatchesPracticalOracleOnEncryptedBlocks) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    auto recursive_controls = HomExpandPackedSwapBits(
        BuildRecursivePackedSwapBitPayload({2, 0, 3, 1}, client_ctx), bundle, server_ctx);
    auto practical_controls = HomExpandPackedSwapBits(
        BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx), bundle, server_ctx);

    // Encrypt blocks, run EvalWaksman() twice, and compare decrypted outputs.
}
```

**Step 2: Run the tests to verify they fail**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'HomExpandTest'
```

Expected:
- recursive semantic comparison still fails.

**Step 3: Implement the lift**

Use the recursive-isolated RLWE ciphertexts plus `A = RGSW(-s)` to produce the final
`RGSWCiphertext` controls needed by `EvalWaksman()`.

Do not remove the practical path.

**Step 4: Run the tests to verify recursive and practical outputs match**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'PermGenTest|HomExpandTest'
```

Expected:
- packed recursive comparisons pass,
- practical path remains green.

**Step 5: Commit**

```bash
git add src/onion_ring/HomExpand.cpp test/onion_ring/homexpand_test.cpp
git commit -m "feat: add recursive rlwe to rgsw lift"
```

### Task 5: Route the live protocol through selectable packed backends

**Files:**
- Modify: `include/oram/onion_ring/OnionRingClient.h`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `include/oram/onion_ring/OnionRingServer.h`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `include/oram/onion_ring/Config.h`
- Modify: `test/onion_ring/onion_ring_e2e_test.cpp`

**Step 1: Write the failing dual-mode end-to-end tests**

Add a recursive-mode end-to-end test configuration with smaller block size and shorter horizon:

```cpp
TEST_F(OnionRingE2ETest, RecursivePackedTransportPreservesBlocksAcrossEvictions) {
    RuntimeConfig cfg = LongRunConfig();
    cfg.block_size = 8;
    cfg.transport_mode = TransportMode::kRecursivePacked;
    StartServer(cfg);

    OnionRingClient client("127.0.0.1", port_, cfg);
    // Write, rewrite, read, and validate.
}
```

Add the scaled long-horizon check:

```cpp
const size_t total_accesses = cfg.a * 10 + 19;
```

**Step 2: Run the tests to verify they fail**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'OnionRingE2ETest'
```

Expected:
- recursive-mode tests fail because protocol handlers do not dispatch by transport mode yet.

**Step 3: Add transport-mode dispatch**

Update the client and server so:

- practical packed mode remains the default,
- recursive packed mode can be selected in tests and manual runs,
- both modes share the same request handlers and only diverge at packed payload generation and
  unpacking.

**Step 4: Run the tests to verify both modes pass**

Run:

```bash
cd worktrees/onion-ring-oram
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R 'OnionRingE2ETest|OnionRingScheduleTest'
```

Expected:
- practical mode still passes,
- recursive mode passes the scaled long-horizon rewrite test.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/OnionRingClient.h src/onion_ring/OnionRingClient.cpp include/oram/onion_ring/OnionRingServer.h src/onion_ring/OnionRingServer.cpp include/oram/onion_ring/Config.h test/onion_ring/onion_ring_e2e_test.cpp
git commit -m "feat: add dual-mode recursive onion ring protocol path"
```

### Task 6: Make recursive mode the new default and document the cutover

**Files:**
- Modify: `include/oram/onion_ring/Config.h`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `README.md`
- Modify: `docs/plans/2026-04-21-onion-ring-homexpand-followup.md`
- Modify: `task.md`

**Step 1: Write the failing default-mode regression**

Add a small regression that instantiates the default config and asserts the recursive path is in
use while the practical path still exists for oracle tests.

**Step 2: Run the regression to verify it fails**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R 'OnionRingE2ETest|HomExpandTest'
```

Expected:
- failure until the default transport mode is switched.

**Step 3: Switch the default and update docs**

- make recursive packed mode the default transport,
- keep the practical path only for tests/debugging until later cleanup,
- update README and follow-up docs to describe the new state,
- update `task.md` to reflect completion.

**Step 4: Run focused verification**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R 'TFHEAdapterTest|PermGenTest|HomExpandTest|OnionRingE2ETest|OnionRingScheduleTest'
```

Expected:
- all focused Onion Ring tests pass under the new default.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/Config.h src/onion_ring/OnionRingClient.cpp src/onion_ring/OnionRingServer.cpp README.md docs/plans/2026-04-21-onion-ring-homexpand-followup.md task.md
git commit -m "feat: switch onion ring to recursive homexpand default"
```

### Task 7: Run final verification

**Files:**
- No code changes expected

**Step 1: Run the focused Onion Ring suite**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure -R 'TFHEAdapterTest|PermGenTest|HomExpandTest|OnionRingE2ETest|OnionRingScheduleTest'
```

Expected:
- all Onion Ring focused tests pass.

**Step 2: Run the full repository suite if time permits**

Run:

```bash
cd worktrees/onion-ring-oram
ctest --test-dir build --output-on-failure
```

Expected:
- no regressions across the repository.

**Step 3: Commit the verification marker if needed**

If any docs or tracker files changed during verification:

```bash
git add README.md task.md docs/plans/2026-04-21-onion-ring-homexpand-followup.md
git commit -m "test: verify recursive homexpand onion ring path"
```

If no files changed, skip the commit.
