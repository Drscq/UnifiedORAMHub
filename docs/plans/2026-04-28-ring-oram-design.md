# Ring ORAM Design

**Date:** 2026-04-28

## Goal

Add a classic Ring ORAM implementation with separate client and server classes. The server owns a binary tree of buckets. The client owns the position map, stash, access counter, and eviction counter.

## Architecture

Ring ORAM will live in a new `oram::ring_oram` namespace rather than reusing `oram::onion_ring`, because Onion Ring ORAM in this repository is the TFHE-backed homomorphic prototype and has a different protocol shape.

The first implementation is an in-process client/server split. `RingORAMServer` stores the tree and exposes bucket/path helpers. `RingORAMClient` implements `core::RAM` and performs `Access`, `ReadPath`, `EvictPath`, `EarlyReshuffle`, `GetBlockOffset`, `ReadBucket`, and `WriteBucket` directly against the server object. This keeps the protocol behavior testable without adding a second network protocol beside Path ORAM.

## Components

- `RuntimeConfig`: user parameters `N`, `L`, `Z`, `S`, `A`, plus `block_size`.
- `RingBlock`: address, mapped leaf, payload bytes, and a dummy sentinel.
- `RingBucket`: public metadata fields matching the requested protocol: `count`, `valids`, `addrs`, `leaves`, `ptrs`, and `data`.
- `RingORAMServer`: binary heap tree of `RingBucket` values and deterministic path/index helpers.
- `RingORAMClient`: position map, stash, randomized leaf assignment, periodic eviction, and early reshuffle.

## Data Flow

`Access(a, op, data')` remaps `a` to a fresh leaf, selects one slot from each bucket on the old path, asks the server to XOR those selected payloads into one aggregate block, removes the block from the stash if it was already local, applies writes, and adds the remapped block to the stash. Every `A` accesses it evicts along the next leaf path. It then early-reshuffles touched buckets whose touch counter reached `S`.

The simulated server supports the XOR technique for online bandwidth: `ReadPath` calls one aggregate server operation over the `L + 1` selected slots. The client tracks the selected dummy payloads locally and XORs those masks away from the aggregate result. If the target block was not selected on the path, the aggregate unmasks to only dummy material and the client falls back to the stash.

`WriteBucket` greedily places up to `Z` eligible stash blocks into a bucket, pads every remaining slot with dummies, samples fresh slot offsets, marks all slots valid, and resets the bucket counter. `ReadBucket` removes all valid real blocks into the stash and pads the logical read count to `Z` by invalidating random valid dummy slots.

## Testing

Tests will cover the user-visible RAM behavior and protocol-specific metadata:

- bucket shape matches `Z + S`, `Z`, and valid-bit requirements,
- write/read/overwrite correctness,
- XOR read-path aggregation with one server response per path,
- periodic eviction preserves data across many accesses,
- early reshuffle resets a hot bucket count,
- a 10x eviction-frequency mixed access test covers `ReadPath`, `EarlyReshuffle`, and `EvictPath` together,
- reverse-style deterministic eviction path helper covers leaves through the persistent counter baseline.
