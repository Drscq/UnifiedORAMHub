# Onion Ring ORAM Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Build a working Onion Ring ORAM prototype that integrates real TFHE and real TCP networking, but defers `HomExpand` by sending direct TGSW swap bits during eviction.

**Architecture:** Add a standalone `onion_ring_core` library beside the existing Path ORAM code. Implement the work in layers: networking helpers, TFHE adapters, homomorphic ops, Waksman permutation, then Onion Ring client/server protocol and tests. Keep runtime-configurable parameters so small end-to-end tests remain practical.

**Tech Stack:** C++17, CMake, GoogleTest, OpenSSL AES-CTR, `tfhe/tfhe` `v1.0.1`, TCP sockets through `NetIO`

---

### Task 1: Add Large-Payload Networking Helpers

**Files:**
- Modify: `include/oram/network/NetIO.h`
- Modify: `src/network/NetIO.cpp`
- Modify: `test/network/net_io_test.cpp`

**Step 1: Write the failing test**

Add a test that sends and receives a large byte vector through `NetIO` using new helper methods:

```cpp
TEST(NetIOTest, SendVecRecvVecRoundTripLargePayload) {
    std::vector<uint8_t> payload(128 * 1024);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i & 0xFF);

    // Server thread receives one vector and echoes it back.
    // Client sends with SendVec and receives with RecvVec.
    EXPECT_EQ(reply, payload);
}
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target oram_tests && ctest --test-dir build --output-on-failure -R NetIOTest`

Expected: build fails because `SendVec` and `RecvVec` are not declared.

**Step 3: Write minimal implementation**

Add these methods:

```cpp
void SendVec(const std::vector<uint8_t>& data);
void RecvVec(std::vector<uint8_t>& data);
```

Implementation:

```cpp
void NetIO::SendVec(const std::vector<uint8_t>& data) {
    uint64_t size = data.size();
    SendData(&size, sizeof(size));
    if (size > 0) SendData(data.data(), size);
}

void NetIO::RecvVec(std::vector<uint8_t>& data) {
    uint64_t size = 0;
    RecvData(&size, sizeof(size));
    data.resize(size);
    if (size > 0) RecvData(data.data(), size);
}
```

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target oram_tests && ctest --test-dir build --output-on-failure -R NetIOTest`

Expected: `NetIOTest` passes.

**Step 5: Commit**

```bash
git add include/oram/network/NetIO.h src/network/NetIO.cpp test/network/net_io_test.cpp
git commit -m "feat: add length-prefixed NetIO vector helpers"
```

### Task 2: Integrate TFHE Into The Build

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `test/CMakeLists.txt`
- Create: `test/onion_ring/tfhe_adapter_test.cpp`

**Step 1: Write the failing test**

Create a TFHE smoke test that instantiates parameters and verifies a trivial roundtrip helper exists:

```cpp
TEST(TFHEAdapterSmokeTest, CanConstructClientContext) {
    auto ctx = oram::onion_ring::TFHEContext::CreateClientContext(
        oram::onion_ring::RuntimeConfig{});
    EXPECT_NE(ctx.tlwe_params, nullptr);
    EXPECT_NE(ctx.tgsw_params, nullptr);
}
```

**Step 2: Run test to verify it fails**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`

Expected: configuration or build fails because Onion Ring targets and headers do not exist yet.

**Step 3: Write minimal implementation**

In `CMakeLists.txt`:

- add `FetchContent` for `tfhe/tfhe`,
- set `SOURCE_SUBDIR src`,
- disable TFHE tests,
- add `onion_ring_core`,
- add TFHE include paths,
- link the best available backend by target detection:

```cmake
if(TARGET tfhe-spqlios-fma)
  set(ONION_RING_TFHE_TARGET tfhe-spqlios-fma)
elseif(TARGET tfhe-spqlios-avx)
  set(ONION_RING_TFHE_TARGET tfhe-spqlios-avx)
else()
  set(ONION_RING_TFHE_TARGET tfhe-nayuki-portable)
endif()
```

In `test/CMakeLists.txt`:

- add `onion_ring_tests`,
- link `onion_ring_core`,
- register tests with `gtest_discover_tests`.

**Step 4: Run test to verify it passes**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target onion_ring_tests -j"$(nproc)"
```

Expected: configuration and compile succeed far enough to build the Onion Ring test target.

**Step 5: Commit**

```bash
git add CMakeLists.txt test/CMakeLists.txt test/onion_ring/tfhe_adapter_test.cpp
git commit -m "build: add TFHE-backed onion ring targets"
```

### Task 3: Add Runtime Configuration And TFHE Adapters

**Files:**
- Create: `include/oram/onion_ring/Config.h`
- Create: `include/oram/onion_ring/TFHEAdapter.h`
- Create: `src/onion_ring/TFHEAdapter.cpp`
- Modify: `test/onion_ring/tfhe_adapter_test.cpp`

**Step 1: Write the failing tests**

Expand the adapter test file with:

```cpp
TEST(TFHEAdapterTest, TlweRoundTripBlockPayload) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    std::vector<uint8_t> message(cfg.block_size, 0x5A);

    auto ct = EncryptBlock(message, ctx);
    auto out = DecryptBlock(ct, ctx, cfg.block_size);

    EXPECT_EQ(out, message);
}

TEST(TFHEAdapterTest, TlweSerializationRoundTrip) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto ct = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x11), ctx);

    auto bytes = ct.Serialize();
    auto restored = RLWECiphertext::Deserialize(bytes, ctx.tlwe_params);

    EXPECT_EQ(DecryptBlock(restored, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x11));
}

TEST(TFHEAdapterTest, TgswBitRoundTrip) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto bit = EncryptBit(true, ctx);
    EXPECT_TRUE(DecryptBit(bit, ctx));
}
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R TFHEAdapter`

Expected: compile fails because the config and adapter APIs do not exist.

**Step 3: Write minimal implementation**

Add:

- `RuntimeConfig` with paper defaults plus test-friendly fields,
- `TFHEContext` client/server factories,
- move-only `RLWECiphertext` and `RGSWCiphertext`,
- `EncodeBlockToPolynomial` and `DecodeBlockFromPolynomial`,
- `EncryptBlock`, `DecryptBlock`, `EncryptBit`, `DecryptBit`,
- serialization using TFHE stream export/import.

Representative encoding helper:

```cpp
for (size_t i = 0; i < block.size(); ++i) {
    poly->coefsT[i] = modSwitchToTorus32(block[i], 256);
}
for (size_t i = block.size(); i < params->N; ++i) {
    poly->coefsT[i] = 0;
}
```

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R TFHEAdapter`

Expected: TFHE adapter tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/Config.h include/oram/onion_ring/TFHEAdapter.h src/onion_ring/TFHEAdapter.cpp test/onion_ring/tfhe_adapter_test.cpp
git commit -m "feat: add onion ring TFHE adapters and runtime config"
```

### Task 4: Implement Homomorphic Operations

**Files:**
- Create: `include/oram/onion_ring/HomOps.h`
- Create: `src/onion_ring/HomOps.cpp`
- Create: `test/onion_ring/hom_ops_test.cpp`

**Step 1: Write the failing tests**

Add tests for:

```cpp
TEST(HomOpsTest, ExternalProductMatchesEncryptedBitSelection);
TEST(HomOpsTest, CMuxSelectsD0WhenControlIsZero);
TEST(HomOpsTest, CMuxSelectsD1WhenControlIsOne);
TEST(HomOpsTest, SubsMatchesPolynomialRotationForSmallAi);
```

Example `CMux` test:

```cpp
auto d0 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x10), ctx);
auto d1 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x20), ctx);
auto c0 = EncryptBit(false, ctx);
auto c1 = EncryptBit(true, ctx);

RLWECiphertext out0(ctx.tlwe_params);
CMux(out0.Get(), c0.Get(), d1.Get(), d0.Get(), ctx.tgsw_params);
EXPECT_EQ(DecryptBlock(out0, ctx, cfg.block_size),
          std::vector<uint8_t>(cfg.block_size, 0x10));
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R HomOpsTest`

Expected: compile fails because `HomOps` does not exist.

**Step 3: Write minimal implementation**

Implement:

- `ExternalProduct` as a direct `tGswExternProduct` wrapper,
- `CMux` as:

```cpp
tLweCopy(result, d1, tlwe_params);
tLweSubTo(result, d0, tlwe_params);
tGswExternProduct(result, C, result, tgsw_params);
tLweAddTo(result, d0, tlwe_params);
```

- `Subs` using TFHE's polynomial rotation primitives where possible, falling back to explicit coefficient remapping when needed.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R HomOpsTest`

Expected: homomorphic operation tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/HomOps.h src/onion_ring/HomOps.cpp test/onion_ring/hom_ops_test.cpp
git commit -m "feat: add onion ring homomorphic operations"
```

### Task 5: Implement Waksman Network Generation And Evaluation

**Files:**
- Create: `include/oram/onion_ring/WaksmanNetwork.h`
- Create: `src/onion_ring/WaksmanNetwork.cpp`
- Create: `test/onion_ring/waksman_test.cpp`

**Step 1: Write the failing tests**

Add plaintext and encrypted tests:

```cpp
TEST(WaksmanTest, GenerateSwapBitsPermutesFourElements);
TEST(WaksmanTest, GenerateSwapBitsPermutesEightElements);
TEST(WaksmanTest, EvalWaksmanMatchesPermutationForEncryptedBlocks);
```

Encrypted evaluation example:

```cpp
std::vector<std::vector<uint8_t>> plain = {
    BlockByte(0x01), BlockByte(0x02), BlockByte(0x03), BlockByte(0x04)
};
std::vector<size_t> permutation = {2, 0, 3, 1};
```

Encrypt each block, encrypt each swap bit as TGSW, evaluate, decrypt, and compare against the permuted plaintext order.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R WaksmanTest`

Expected: compile fails because `WaksmanNetwork` does not exist.

**Step 3: Write minimal implementation**

Implement:

- recursive gate construction,
- swap-bit generation,
- in-place encrypted evaluation using two `CMux` calls per gate.

Representative evaluation block:

```cpp
CMux(left_tmp.Get(), swap_bits[gate.bit_idx], data[gate.j], data[gate.i], params);
CMux(right_tmp.Get(), swap_bits[gate.bit_idx], data[gate.i], data[gate.j], params);
```

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R WaksmanTest`

Expected: Waksman tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/WaksmanNetwork.h src/onion_ring/WaksmanNetwork.cpp test/onion_ring/waksman_test.cpp
git commit -m "feat: add onion ring waksman permutation network"
```

### Task 6: Add Onion Bucket And Access-Only Client/Server Skeleton

**Files:**
- Create: `include/oram/onion_ring/OnionBucket.h`
- Create: `include/oram/onion_ring/OnionRingClient.h`
- Create: `include/oram/onion_ring/OnionRingServer.h`
- Create: `src/onion_ring/OnionRingClient.cpp`
- Create: `src/onion_ring/OnionRingServer.cpp`
- Create: `test/onion_ring/onion_ring_e2e_test.cpp`

**Step 1: Write the failing tests**

Start with access-only end-to-end tests:

```cpp
TEST_F(OnionRingE2ETest, WriteThenReadSingleBlock);
TEST_F(OnionRingE2ETest, OverwriteBlockReturnsLatestValue);
TEST_F(OnionRingE2ETest, MultipleWritesAndReadsWorkBeforeEvictionThreshold);
```

Use a small runtime config:

```cpp
RuntimeConfig cfg;
cfg.z = 4;
cfg.s = 4;
cfg.a = 3;
cfg.tree_height = 3;
cfg.num_blocks = 8;
cfg.block_size = 32;
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: compile fails because Onion Ring client/server code does not exist.

**Step 3: Write minimal implementation**

Implement:

- `OnionBucket` with `2Z` slots,
- server `Init`,
- command handling for:
  - `'A'` access,
  - `'Q'` quit,
- client `Access`, `Read`, `Write`,
- path indexing and slot selection metadata,
- access aggregation by TLWE sum.

Keep eviction disabled for the first green cycle by only supporting access counts below `a`.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: access-only end-to-end tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/OnionBucket.h include/oram/onion_ring/OnionRingClient.h include/oram/onion_ring/OnionRingServer.h src/onion_ring/OnionRingClient.cpp src/onion_ring/OnionRingServer.cpp test/onion_ring/onion_ring_e2e_test.cpp
git commit -m "feat: add onion ring access protocol skeleton"
```

### Task 7: Implement Triplet Eviction With Direct TGSW Swap Bits

**Files:**
- Modify: `include/oram/onion_ring/OnionRingClient.h`
- Modify: `include/oram/onion_ring/OnionRingServer.h`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `test/onion_ring/onion_ring_e2e_test.cpp`

**Step 1: Write the failing tests**

Add eviction-triggering tests:

```cpp
TEST_F(OnionRingE2ETest, EvictionPreservesBlocksAcrossManyAccesses);
TEST_F(OnionRingE2ETest, ReverseBitEvictionScheduleCoversLeaves);
```

The first test should perform enough reads and writes to cross multiple `a` boundaries and verify the oracle contents afterward.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: access-only implementation fails once eviction is required.

**Step 3: Write minimal implementation**

Implement:

- reverse-bit eviction scheduling,
- client-side triplet partitioning,
- permutation generation,
- direct TGSW swap-bit encryption and transmission,
- server-side padded child buckets,
- `EvalWaksman` application over child slot arrays.

Protocol payload for triplet eviction:

```cpp
'T' + source_idx + left_idx + right_idx + vector<TGSW ciphertext bytes>
```

Use `SendVec` and `RecvVec` for each serialized swap-bit ciphertext.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: eviction tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/OnionRingClient.h include/oram/onion_ring/OnionRingServer.h src/onion_ring/OnionRingClient.cpp src/onion_ring/OnionRingServer.cpp test/onion_ring/onion_ring_e2e_test.cpp
git commit -m "feat: add onion ring triplet eviction with direct tgsw bits"
```

### Task 8: Implement Leaf Refresh And Complete The Protocol

**Files:**
- Modify: `include/oram/onion_ring/OnionRingClient.h`
- Modify: `include/oram/onion_ring/OnionRingServer.h`
- Modify: `src/onion_ring/OnionRingClient.cpp`
- Modify: `src/onion_ring/OnionRingServer.cpp`
- Modify: `test/onion_ring/onion_ring_e2e_test.cpp`

**Step 1: Write the failing tests**

Add:

```cpp
TEST_F(OnionRingE2ETest, LeafRefreshReencryptsAndPreservesData);
TEST_F(OnionRingE2ETest, MixedAccessesRemainCorrectAfterRefreshAndEviction);
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: tests fail because the leaf refresh portion of the protocol is incomplete.

**Step 3: Write minimal implementation**

Implement:

- `'L'` request,
- `'B'` bucket transfer,
- `'F'` refreshed bucket write-back,
- client-side decrypt/re-encrypt flow,
- fresh random leaf permutation generation,
- server-side replacement and Waksman evaluation.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target onion_ring_tests && ctest --test-dir build --output-on-failure -R OnionRingE2ETest`

Expected: all Onion Ring end-to-end tests pass.

**Step 5: Commit**

```bash
git add include/oram/onion_ring/OnionRingClient.h include/oram/onion_ring/OnionRingServer.h src/onion_ring/OnionRingClient.cpp src/onion_ring/OnionRingServer.cpp test/onion_ring/onion_ring_e2e_test.cpp
git commit -m "feat: complete onion ring leaf refresh protocol"
```

### Task 9: Run Full Verification

**Files:**
- Modify: none unless fixes are needed
- Test: `test/onion_ring/*.cpp`

**Step 1: Run Onion Ring tests**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target onion_ring_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R "(TFHEAdapter|HomOpsTest|WaksmanTest|OnionRingE2ETest)"
```

Expected: all Onion Ring tests pass.

**Step 2: Run existing regression suite**

Run:

```bash
cmake --build build --target oram_tests -j"$(nproc)"
ctest --test-dir build --output-on-failure -R "(NetIOTest|AES|PathORAM)"
```

Expected: existing Path ORAM and utility tests still pass.

**Step 3: Spot-check build configuration**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target onion_ring_tests -j"$(nproc)"
```

Expected: release configuration also builds.

**Step 4: Commit**

```bash
git add .
git commit -m "test: verify onion ring integration end to end"
```

### Task 10: Prepare The Optimization Follow-Up

**Files:**
- Modify: `docs/plans/2026-04-20-onion-ring-oram-design.md`
- Create: `docs/plans/2026-04-20-onion-ring-homexpand-followup.md`

**Step 1: Write the failing test placeholder**

Create a disabled or documented placeholder for packed swap-bit transport:

```cpp
TEST(WaksmanTest, DISABLED_PackedSwapBitsViaHomExpand) {
    GTEST_SKIP() << "Planned after direct-TGSW prototype is stable";
}
```

**Step 2: Run test to verify current status**

Run: `ctest --test-dir build --output-on-failure -R WaksmanTest`

Expected: all active tests pass and the packed-transport follow-up remains skipped.

**Step 3: Write follow-up doc**

Document:

- packed RLWE swap-bit transport,
- `HomExpand`,
- required TFHE gaps,
- measurement plan for communication reduction.

**Step 4: Commit**

```bash
git add docs/plans/2026-04-20-onion-ring-oram-design.md docs/plans/2026-04-20-onion-ring-homexpand-followup.md test/onion_ring/waksman_test.cpp
git commit -m "docs: capture homexpand optimization follow-up"
```
