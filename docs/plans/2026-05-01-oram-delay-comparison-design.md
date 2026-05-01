# ORAM Delay Comparison Design

## Goal

Add a small, reproducible benchmark helper for comparing Path ORAM and Ring ORAM end-to-end network delay under the user's supplied parameters without allocating or running the full database.

## Approach

Use an analytical network model rather than a live ORAM workload. The protocol mechanics are specified in bytes, round trips, bandwidth, and eviction frequency, so the script can compute the same end-to-end delay deterministically and scale to larger databases by changing CLI arguments.

The default run models `10 * A` accesses, which is `480` accesses for Ring ORAM's default `A = 48`. It evaluates 4 KiB, 8 KiB, and 16 KiB blocks, 16 tree levels, 5 ms RTT latency, and 50 Mbps bandwidth.

## Formulas

Path ORAM per access:

- round trips: `2`
- transfer bits: `2 * levels * Z * block_bits`

Ring ORAM over a workload:

- online round trips: `1` per access
- online transfer bits: `block_bits` per access
- eviction count: `floor(accesses / A)`
- eviction transfer bits: `levels * (Z + Z + S) * block_bits` per eviction
- eviction round trips: default `2` per eviction for a batched path read/write, with a CLI option for `2 * levels` per eviction when modeling per-bucket read/write RTTs

Total delay is `round_trips * latency + transfer_bits / bandwidth`.

## Files

- `scripts/oram_delay_comparison.py`: self-contained CLI and importable calculation functions.
- `test/scripts/test_oram_delay_comparison.py`: Python `unittest` coverage for the default formulas and CLI output.
- `README.md`: short usage example for the small default run and larger custom runs.

## Error Handling

The script validates positive numeric inputs, rejects non-positive eviction rates, and rejects unsupported output formats or Ring eviction RTT modes through `argparse` choices.

## Testing

Use Python standard-library tests so the benchmark helper has no extra dependency. The C++ ORAM tests are not required for this analytical script, but the script tests should run from the repository root.
