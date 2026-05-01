# Real C++ ORAM Delay Benchmark Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Build a C++ benchmark executable and wrapper script that compare Path ORAM and Ring ORAM end-to-end delay using real TCP communication, AES-CTR encryption/decryption, bucket serialization, and generated path-only server storage.

**Architecture:** Add a small benchmark library under `include/oram/benchmark` and `src/benchmark`, a C++ executable under `benchmarks/`, and focused GoogleTest coverage. The benchmark starts an in-process server thread for each algorithm/block-size run and times client operations through `network::NetIO`, relying on externally prepared `tc` limits.

**Tech Stack:** C++17, existing `network::NetIO`, existing `crypto::AES_CTR`, GoogleTest, Python wrapper script for build/run convenience.

---

### Task 1: Benchmark Planning Tests

**Files:**
- Create: `include/oram/benchmark/ORAMDelayBenchmark.h`
- Create: `test/benchmark/oram_delay_benchmark_test.cpp`
- Modify: `test/CMakeLists.txt`

**Step 1: Write failing tests**

Add tests for:
- default config uses 480 accesses and block sizes 4096, 8192, 16384,
- Path traffic plan for one access has one read path and one write path with `levels * path_z * block_size` bytes each,
- Ring eviction count for 480 accesses and `A=48` is 10,
- a tiny benchmark smoke run returns positive Path and Ring timings.

**Step 2: Run tests to verify failure**

Run: `cmake --build build --target oram_tests -j2`

Expected: FAIL because the benchmark header and APIs do not exist yet.

### Task 2: Benchmark Core

**Files:**
- Modify: `include/oram/benchmark/ORAMDelayBenchmark.h`
- Create: `src/benchmark/ORAMDelayBenchmark.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Add benchmark configuration/results**

Define `BenchmarkConfig`, `AlgorithmResult`, `ComparisonResult`, `TrafficPlan`, and helper functions for default config and byte counts.

**Step 2: Add shared serialization/crypto helpers**

Implement benchmark-local block and bucket serialization helpers with runtime block size, using existing AES-CTR for encrypt/decrypt.

**Step 3: Add Path benchmark client/server**

Implement batched read-path and write-path benchmark commands over `NetIO`.

**Step 4: Add Ring benchmark client/server**

Implement online XOR reads and periodic eviction commands over `NetIO`.

**Step 5: Add comparison runner**

Implement `RunComparison(config)` and `RunAllComparisons(config)`.

### Task 3: Executable and Wrapper

**Files:**
- Create: `benchmarks/oram_real_delay_benchmark.cpp`
- Create: `scripts/oram_real_delay_benchmark.py`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `test/scripts/test_oram_real_delay_benchmark.py`

**Step 1: Add C++ executable**

Create a CLI that accepts block sizes, accesses, host, start port, and format options.

**Step 2: Add Python wrapper**

Create a wrapper that builds `oram_real_delay_benchmark` and forwards benchmark arguments.

**Step 3: Add script tests**

Test dry-run/build command construction without running the full benchmark.

**Step 4: Document commands**

Add README instructions for running after applying `tc`.

### Task 4: Verification and Requested Run

**Files:**
- Modify as needed: benchmark code/tests/docs

**Step 1: Run C++ tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure -j1`

Expected: PASS.

**Step 2: Run script tests**

Run: `python3 -m unittest discover -s test/scripts -v`

Expected: PASS.

**Step 3: Run the requested benchmark**

Run: `python3 scripts/oram_real_delay_benchmark.py`

Expected: Prints Path/Ring comparison for 4 KiB, 8 KiB, and 16 KiB using the currently active traffic limits.
