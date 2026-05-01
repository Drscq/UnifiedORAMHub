# Disk-Backed Path ORAM Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Store Path ORAM server buckets in disk files instead of keeping the server database only in memory.

**Architecture:** Add an optional server storage directory to `PathORAMServer`. Initialize one serialized bucket file per tree node and change read/write handlers to load and replace bucket files on demand.

**Tech Stack:** C++17, `std::filesystem`, `std::fstream`, existing `core::Serializer`, GoogleTest.

---

### Task 1: Disk-Backed Server Tests

**Files:**
- Modify: `test/path_oram/path_oram_test.cpp`
- Modify: `include/oram/path_oram/PathORAMServer.h`

**Step 1: Write the failing test**

Add a Path ORAM disk-backed test that starts a server with an explicit temporary storage directory, performs a normal write/read through `PathORAMClient`, and asserts:
- `storage_dir/tree/bucket_0.bin` exists,
- `storage_dir/tree/bucket_<NumTreeNodes - 1>.bin` exists,
- the number of regular files in `storage_dir/tree` equals `Config::GetNumTreeNodes(kTreeHeight)`,
- each inspected bucket file is nonempty.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target oram_tests -j2`

Expected: FAIL at compile time because `PathORAMServer` does not accept a storage directory argument.

### Task 2: Server Bucket File Store

**Files:**
- Modify: `include/oram/path_oram/PathORAMServer.h`
- Modify: `src/path_oram/PathORAMServer.cpp`

**Step 1: Add storage state**

Add `storage_dir_`, `tree_dir_`, and `node_count_` members. Remove the persistent `tree_` member.

**Step 2: Add file helpers**

Add helpers for default storage path resolution, bucket file paths, initialization validation, bucket shape validation, bucket file reads, and bucket file writes.

**Step 3: Persist initialized buckets**

Change `Init()` to recreate the tree directory and write all buckets to disk.

**Step 4: Use disk in protocol handlers**

Change read and write handlers to call the disk helpers instead of indexing an in-memory tree.

**Step 5: Run focused tests**

Run: `cmake --build build --target oram_tests -j2 && ./build/test/oram_tests --gtest_filter='PathORAMDiskBackedTest.*'`

Expected: PASS.

### Task 3: Full Regression Verification

**Files:**
- Modify as needed: Path ORAM implementation and tests

**Step 1: Run Path ORAM tests**

Run: `./build/test/oram_tests --gtest_filter='PathORAM*'`

Expected: PASS.

**Step 2: Run all tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure -j1`

Expected: PASS.
