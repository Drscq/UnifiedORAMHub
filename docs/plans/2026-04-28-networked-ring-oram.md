# Networked Ring ORAM Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Replace the in-process Ring ORAM simulation with a socket-based client/server protocol where the server stores encrypted buckets and never shares memory with the client.

**Architecture:** `RingORAMServer` becomes a `NetIO` command server storing `EncryptedRingBucket` objects. `RingORAMClient` connects over `NetIO`, generates and encrypts the initial tree, sends commands for metadata reads, XOR path reads, bucket reads, bucket writes, and public count queries, and performs all encryption/decryption locally.

**Tech Stack:** C++17, existing `oram::network::NetIO`, existing `oram::crypto::AES_CTR`, CMake, GoogleTest.

---

### Task 1: Networked Tests

**Files:**
- Modify: `test/ring_oram/ring_oram_test.cpp`

**Step 1: Write failing tests**

Refactor tests to start `RingORAMServer` in a thread and construct `RingORAMClient` with `address, port, config`. Add assertions that the constructor initializes server storage over the socket and that access uses one XOR aggregate per read path.

**Step 2: Run test to verify failure**

Run: `cmake --build build --target ring_oram_tests -j2`

Expected: compile failure because the networked constructors and protocol query helpers do not exist.

### Task 2: Encrypted Bucket Wire Model

**Files:**
- Modify: `include/oram/ring_oram/RingBucket.h`
- Modify: `src/ring_oram/RingBucket.cpp`

**Step 1: Add encrypted structs and serializers**

Add encrypted field structs for AES IV plus ciphertext, `EncryptedRingBucket`, and serialization/deserialization helpers for encrypted buckets and path metadata.

**Step 2: Run test to verify progress**

Run: `cmake --build build --target ring_oram_tests -j2`

Expected: compile proceeds farther and fails on networked client/server APIs.

### Task 3: Networked Server

**Files:**
- Modify: `include/oram/ring_oram/RingORAMServer.h`
- Modify: `src/ring_oram/RingORAMServer.cpp`

**Step 1: Implement `NetIO` command loop**

Add commands:
- `I`: initialize encrypted tree
- `M`: read encrypted path metadata
- `X`: XOR selected encrypted slots, invalidate selected slots, increment counts
- `B`: read encrypted bucket
- `W`: write encrypted bucket
- `T`: read public bucket count
- `S`: read protocol stats
- `Q`: quit

**Step 2: Run test to verify progress**

Run: `cmake --build build --target ring_oram_tests -j2`

Expected: compile proceeds farther and fails on client command implementation.

### Task 4: Networked Client

**Files:**
- Modify: `include/oram/ring_oram/RingORAMClient.h`
- Modify: `src/ring_oram/RingORAMClient.cpp`

**Step 1: Implement client socket protocol**

Add network constructor/destructor, AES-CTR encryption helpers, initial encrypted tree generation, metadata fetch/decrypt, XOR path command/decrypt/unmask, encrypted bucket read/write, public count query, and server stats query.

**Step 2: Run focused tests**

Run: `cmake --build build --target ring_oram_tests -j2 && ./build/test/ring_oram_tests`

Expected: PASS.

### Task 5: Full Verification

**Files:**
- Modify as needed: implementation files from previous tasks

**Step 1: Run all tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure -j1`

Expected: PASS.
