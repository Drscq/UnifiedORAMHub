# Ring ORAM Design

**Date:** 2026-04-28

## Goal

Add a classic Ring ORAM implementation with separate client and server classes. The server owns a binary tree of buckets. The client owns the position map, stash, access counter, and eviction counter.

## Architecture

Ring ORAM will live in a new `oram::ring_oram` namespace rather than reusing `oram::onion_ring`, because Onion Ring ORAM in this repository is the TFHE-backed homomorphic prototype and has a different protocol shape.

The implementation must use a strict socket boundary. `RingORAMServer` owns only encrypted serialized bucket storage and a `NetIO` server connection. `RingORAMClient` owns the encryption key, position map, stash, local metadata decoding, and a `NetIO` client connection. The server must not share address space references with the client.

## Components

- `RuntimeConfig`: user parameters `N`, `L`, `Z`, `S`, `A`, plus `block_size`.
- `RingBlock`: address, mapped leaf, payload bytes, and a dummy sentinel.
- `RingBucket`: public metadata fields matching the requested protocol: `count`, `valids`, `addrs`, `leaves`, `ptrs`, and `data`.
- `EncryptedRingBucket`: plaintext `count` and `valids`, encrypted `addrs`, `leaves`, `ptrs`, and encrypted slot data.
- `RingORAMServer`: binary heap tree of `EncryptedRingBucket` values, deterministic path/index helpers, and a command loop over `NetIO`.
- `RingORAMClient`: position map, stash, randomized leaf assignment, periodic eviction, early reshuffle, AES-CTR encryption, and socket commands over `NetIO`.

## Data Flow

At startup the client generates the position map, builds an encrypted binary tree, and sends it to the server with an init command. Real zero blocks that do not fit in the initialized tree remain in the client stash.

`Access(a, op, data')` remaps `a` to a fresh leaf, fetches encrypted path metadata, selects one slot from each bucket on the old path, asks the server to XOR those selected encrypted payloads into one aggregate ciphertext, removes the block from the stash if it was already local, applies writes, and adds the remapped block to the stash. Every `A` accesses it evicts along the next leaf path. It then early-reshuffles touched buckets whose public touch counter reached `S`.

The server supports the XOR technique for online bandwidth: `ReadPath` calls one aggregate server operation over the `L + 1` selected encrypted slots. The client reconstructs the selected encrypted dummy blocks and XORs those ciphertext masks away from the aggregate result. If the target block was not selected on the path, the client falls back to the stash.

`WriteBucket` greedily places up to `Z` eligible stash blocks into a bucket, pads every remaining slot with dummies, samples fresh slot offsets, encrypts metadata and data under fresh IVs, marks all slots valid, resets the bucket counter, and sends the encrypted bucket to the server. `ReadBucket` downloads a full encrypted bucket for eviction or early reshuffle, decrypts all valid real blocks into the stash, and pads the logical read count to `Z` locally before the bucket is rewritten.

## Testing

Tests will cover the user-visible RAM behavior and protocol-specific metadata:

- bucket shape matches `Z + S`, `Z`, and valid-bit requirements,
- write/read/overwrite correctness,
- XOR read-path aggregation with one server response per path,
- periodic eviction preserves data across many accesses,
- early reshuffle resets a hot bucket count,
- a 10x eviction-frequency mixed access test covers `ReadPath`, `EarlyReshuffle`, and `EvictPath` together,
- reverse-style deterministic eviction path helper covers leaves through the persistent counter baseline.
