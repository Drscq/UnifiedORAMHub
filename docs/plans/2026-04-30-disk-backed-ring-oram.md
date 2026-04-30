# Disk-Backed Ring ORAM Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Store Ring ORAM server tree buckets and the client stash in per-run disk files instead of keeping them only in memory.

**Architecture:** Add optional storage paths to `oram::ring_oram::RuntimeConfig`. The server persists each encrypted bucket as its own serialized file and loads buckets on demand. The client persists the plaintext stash as a serialized file and reloads/saves it around stash operations while preserving the existing public API.

**Tech Stack:** C++17, `std::filesystem`, `std::fstream`, existing Ring ORAM serializers, GoogleTest.

---

### Task 1: Disk-Backed Storage Tests

**Files:**
- Modify: `test/ring_oram/ring_oram_test.cpp`
- Modify: `include/oram/ring_oram/Config.h`
- Modify: `include/oram/ring_oram/RingORAMClient.h`
- Modify: `include/oram/ring_oram/RingORAMServer.h`

**Step 1: Write the failing tests**

Add helper functions that create unique temp paths and clean them up in `TearDown`.

Add a test that configures `server_storage_dir` and `stash_file_path`, starts a server, creates a client, then checks:
- `server_storage_dir/tree/bucket_0.bin` exists,
- `server_storage_dir/tree/bucket_<NumTreeNodes - 1>.bin` exists,
- the number of regular files in `server_storage_dir/tree` equals `cfg.NumTreeNodes()`,
- `stash_file_path` exists and is a regular file.

Add a second test that writes and reads a block, then checks the stash file exists and has a nonzero size.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target ring_oram_tests -j2`

Expected: FAIL at compile time because storage path fields and path accessors do not exist.

### Task 2: Server Bucket File Store

**Files:**
- Modify: `include/oram/ring_oram/Config.h`
- Modify: `include/oram/ring_oram/RingORAMServer.h`
- Modify: `src/ring_oram/RingORAMServer.cpp`

**Step 1: Add storage configuration**

Add optional `std::string server_storage_dir` and `std::string stash_file_path` fields to `RuntimeConfig`.

**Step 2: Implement server file helpers**

Replace `tree_` with:
- `std::string storage_dir_`
- `std::string tree_dir_`
- `size_t node_count_ = 0`

Add helpers:
- `ResolveServerStorageDir(address, port, config)`
- `BucketPath(bucket_idx)`
- `ReadBucketFromDisk(bucket_idx)`
- `WriteBucketToDisk(bucket_idx, bucket)`
- `ValidateBucketShape(bucket, context)`

**Step 3: Persist buckets**

Update init, metadata reads, XOR path reads, full bucket reads, full bucket writes, count reads, `NumNodes()`, `ValidateInitialized()`, and node index validation to use file-backed storage.

**Step 4: Run focused tests**

Run: `cmake --build build --target ring_oram_tests -j2 && ./build/test/ring_oram_tests --gtest_filter='*Disk*:*Initializes*'`

Expected: server disk tests compile and progress to stash failures if the client side is not done yet.

### Task 3: Client Stash File Store

**Files:**
- Modify: `include/oram/ring_oram/RingORAMClient.h`
- Modify: `src/ring_oram/RingORAMClient.cpp`
- Modify: `src/ring_oram/RingBucket.cpp`
- Modify: `include/oram/ring_oram/RingBucket.h`

**Step 1: Add stash serialization**

Add `SerializeRingBlock`, `DeserializeRingBlock`, `SerializeRingBlocks`, and `DeserializeRingBlocks` helpers for `RingBlock` vectors.

**Step 2: Add client file helpers**

Add:
- `std::string stash_file_path_`
- `ResolveStashFilePath(server_address, port, config)`
- `LoadStashFromDisk()`
- `SaveStashToDisk()`

**Step 3: Make stash operations disk-backed**

Persist the initial stash before server initialization. Reload before reads from the stash, reload/save around `PutBlockInStash`, `TakeBlockFromStash`, `RemoveBlockFromStash`, and `FillBucketFromStash`, and keep `stash_` as the last loaded snapshot for existing tests.

**Step 4: Run focused tests**

Run: `cmake --build build --target ring_oram_tests -j2 && ./build/test/ring_oram_tests --gtest_filter='*Disk*'`

Expected: PASS.

### Task 4: Full Regression Verification

**Files:**
- Modify as needed: Ring ORAM implementation and tests

**Step 1: Run Ring ORAM tests**

Run: `cmake --build build --target ring_oram_tests -j2 && ./build/test/ring_oram_tests`

Expected: PASS.

**Step 2: Run all tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure -j1`

Expected: PASS.
