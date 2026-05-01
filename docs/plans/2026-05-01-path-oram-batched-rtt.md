# Path ORAM Batched RTT Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Make Path ORAM accesses use one read-path request/response and one write-path request/response.

**Architecture:** Add path-level commands to the existing Path ORAM TCP protocol. Keep current AES-CTR encryption, bucket serialization, stash eviction, and disk-backed server storage, but batch all buckets on a path into one read response and one write request with acknowledgement.

**Tech Stack:** C++17, existing `network::NetIO`, existing `core::Serializer`, existing `crypto::AES_CTR`, GoogleTest.

---

### Task 1: Protocol-Shape Test

**Files:**
- Modify: `include/oram/path_oram/PathORAMServer.h`
- Modify: `test/path_oram/path_oram_test.cpp`

**Step 1: Write the failing test**

Add a Path ORAM test that starts a real server, performs exactly one `client.Write(...)`, then reads server request stats after shutdown. Assert:
- `stats.read_path_requests == 1`
- `stats.write_path_requests == 1`
- `stats.read_bucket_requests == 0`
- `stats.write_bucket_requests == 0`

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target oram_tests -j2`

Expected: FAIL at compile time because `PathORAMServer` does not expose request stats yet.

### Task 2: Server Stats and Batched Handlers

**Files:**
- Modify: `include/oram/path_oram/PathORAMServer.h`
- Modify: `src/path_oram/PathORAMServer.cpp`

**Step 1: Add stats state**

Add a public `RequestStats` struct and `GetRequestStats()` getter. Increment single-bucket counters in existing handlers and batched counters in the new path handlers.

**Step 2: Add path read handler**

Add a command that receives a bucket-count and bucket indices, reads/encrypts each bucket, and replies once with all encrypted buckets.

**Step 3: Add path write handler**

Add a command that receives bucket-count plus encrypted bucket payloads, decrypts/stores each bucket, and sends one acknowledgement byte after all writes finish.

### Task 3: Client Batched Read/Write

**Files:**
- Modify: `include/oram/path_oram/PathORAMClient.h`
- Modify: `src/path_oram/PathORAMClient.cpp`

**Step 1: Add client helpers**

Add private helpers to read and write a whole path over the new commands.

**Step 2: Switch access path operations**

Change `ReadPath()` and `WritePath()` to call the batched helpers instead of looping through single-bucket helpers.

**Step 3: Run focused test**

Run: `cmake --build build --target oram_tests -j2 && ./build/test/oram_tests --gtest_filter='PathORAMProtocolTest.AccessUsesOneReadPathAndOneWritePathRequest'`

Expected: PASS.

### Task 4: Regression Verification

**Files:**
- Modify as needed: Path ORAM implementation and tests

**Step 1: Run Path ORAM tests**

Run: `./build/test/oram_tests --gtest_filter='PathORAM*'`

Expected: PASS.

**Step 2: Run full C++ tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure -j1`

Expected: PASS.
