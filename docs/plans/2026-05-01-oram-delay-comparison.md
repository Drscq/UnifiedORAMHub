# ORAM Delay Comparison Implementation Plan

> **For the agent:** Use the `superpowers-executing-plans` skill to implement this plan task-by-task.

**Goal:** Add a configurable script that computes end-to-end delay comparisons for Path ORAM and Ring ORAM under specified network constraints.

**Architecture:** Implement a dependency-free Python CLI with importable pure calculation functions. Cover the formulas with Python `unittest` tests, then document the run commands in `README.md`.

**Tech Stack:** Python 3 standard library, `argparse`, `dataclasses`, `unittest`.

---

### Task 1: Failing Delay Model Tests

**Files:**
- Create: `test/scripts/test_oram_delay_comparison.py`
- Create: `scripts/oram_delay_comparison.py`

**Step 1: Write the failing test**

Create `test/scripts/test_oram_delay_comparison.py` with tests that import `scripts/oram_delay_comparison.py` and assert:

- default access count is `480`,
- Path ORAM 4 KiB amortized delay is `93.88608` ms/access,
- Ring ORAM 4 KiB amortized delay is `30.767373333333335` ms/access in path-batched eviction RTT mode,
- CLI CSV output includes rows for `4`, `8`, and `16` KiB.

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest discover -s test/scripts -p 'test_oram_delay_comparison.py' -v`

Expected: FAIL or ERROR because the script does not exist yet.

### Task 2: Implement Benchmark Script

**Files:**
- Modify: `scripts/oram_delay_comparison.py`
- Modify: `test/scripts/test_oram_delay_comparison.py`

**Step 1: Write minimal implementation**

Implement:

- `NetworkConfig`, `PathORAMConfig`, `RingORAMConfig`, and `DelayResult` dataclasses,
- `path_oram_delay(...)`,
- `ring_oram_delay(...)`,
- default scenario construction,
- `markdown`, `csv`, and `json` output formats,
- CLI flags for database size, levels, block sizes, latency, bandwidth, access count, and ORAM parameters.

**Step 2: Run tests to verify they pass**

Run: `python3 -m unittest discover -s test/scripts -p 'test_oram_delay_comparison.py' -v`

Expected: PASS.

### Task 3: Document Run Commands

**Files:**
- Modify: `README.md`

**Step 1: Add usage section**

Document:

- default small comparison command,
- CSV command for saving results,
- larger database command showing `--num-blocks`, `--levels`, and custom block sizes.

**Step 2: Run script manually**

Run: `python3 scripts/oram_delay_comparison.py`

Expected: table with Path ORAM and Ring ORAM rows for 4, 8, and 16 KiB.

### Task 4: Final Verification

**Files:**
- Verify all touched files.

**Step 1: Run Python tests**

Run: `python3 -m unittest discover -s test/scripts -p 'test_oram_delay_comparison.py' -v`

Expected: PASS.

**Step 2: Run script CSV smoke test**

Run: `python3 scripts/oram_delay_comparison.py --format csv`

Expected: CSV header and six result rows.
