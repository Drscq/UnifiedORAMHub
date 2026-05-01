# Real C++ ORAM Delay Benchmark Design

**Date:** 2026-05-01

## Goal

Add a C++ benchmark that compares Path ORAM and Ring ORAM end-to-end access delay under the requested network limits while exercising real client/server communication, AES-CTR encryption/decryption, bucket serialization, and server-side path storage.

## Current State

The existing `scripts/oram_delay_comparison.py` is analytical only. The existing `scripts/oram_tcp_benchmark.py` sends modeled TCP payloads, but it does not execute ORAM client computation, server storage operations, encryption, or decryption.

The full C++ ORAM implementations are also not a good benchmark substrate for the requested `N=2**16` and 4/8/16 KiB blocks. Ring ORAM would materialize a very large tree because each bucket has `Z + S = 81` slots. Path ORAM now has the intended batched path RTT protocol, but its production config still has a compile-time 256-byte block size.

## Approach

The benchmark will add a new C++ path-only real-execution harness. It will generate the path data needed for each access instead of materializing the whole binary tree. This matches the requested measurement scope: the user cares about access delay and explicitly does not need the full database generated for the benchmark.

Path ORAM executes, per access, one batched read-path request and one batched write-path request. The server stores one generated 16-level path with `Z=4` blocks per bucket. For each access, the server serializes and encrypts each bucket, the client receives/decrypts/scans the path, mutates one block-shaped payload, encrypts write-back buckets, and the server decrypts and stores them.

Ring ORAM executes, per access, one online XOR read-path request that returns exactly one block-sized payload. Every `A=48` accesses, it also executes a real eviction request: the server sends `Z=33` blocks per bucket over the 16-level path, the client decrypts/processes/re-encrypts `Z+S=81` blocks per bucket, and the server decrypts and stores the written path. The final Ring delay is reported as measured online time plus measured eviction time amortized over the access count.

## Network Model

The benchmark uses localhost TCP through the existing `network::NetIO`, so the user's `tc` shaping on loopback is in the timing path. The benchmark does not apply `tc` itself. It assumes the user already applied `5 ms` RTT and `50 Mbps` bandwidth.

## Output

The executable will default to the requested configuration:
- `N = 2**16`
- levels = 16
- accesses = `10 * A = 480`
- block sizes = 4 KiB, 8 KiB, 16 KiB
- Path `Z=4`
- Ring `Z=33`, `S=48`, `A=48`

For each block size it will print Path ORAM average access delay, Ring ORAM amortized average access delay, Ring online and eviction components, and the Path/Ring speedup ratio.

## Testing

Unit tests will cover request planning formulas and a tiny loopback benchmark run with small parameters so the test remains fast without relying on `tc`. The full requested benchmark is a manual/benchmark command, not part of the unit test suite.
