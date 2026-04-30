# Disk-Backed Ring ORAM Design

**Date:** 2026-04-30

## Goal

Move Ring ORAM's server tree and client stash backing storage from process-only RAM to per-run files on disk, while keeping the current socket protocol and ORAM behavior intact.

## Alternatives Considered

The recommended approach is a lightweight file-backed store: one serialized encrypted bucket file per tree node on the server, and one serialized stash file on the client. This fits the current per-bucket protocol, avoids a new dependency, and makes disk I/O explicit and easy to test.

A single appendable database file was considered, but random bucket rewrites would require a fixed-record format or an index layer. That is unnecessary for the current variable-length encrypted bucket serialization.

SQLite or RocksDB was also considered. It would provide durable key-value semantics, but adds build complexity and hides the simple storage behavior this implementation needs.

## Architecture

`RuntimeConfig` gains optional storage path fields. If callers leave them empty, the server and client derive per-run paths under the system temp directory using the server address and port. Tests can set explicit paths to inspect the files.

`RingORAMServer` no longer owns `std::vector<EncryptedRingBucket> tree_`. It owns a storage directory and a small initialized node count. Each server operation loads only the bucket it needs from `bucket_<index>.bin`, mutates it if required, and writes it back.

`RingORAMClient` keeps the existing public `Stash()` accessor for tests, but treats a stash file as the source of truth. Helper methods load the stash before every stash-dependent operation and save it after each mutation. The in-memory vector becomes a short-lived cache of the on-disk stash contents.

## Data Flow

During initialization, the client builds the initial plaintext tree as before, encrypts each bucket, and sends it to the server. The server writes each encrypted bucket directly to its bucket file instead of pushing it into a vector.

Path metadata reads load each bucket on the requested path and serialize metadata responses. XOR path reads load each selected bucket, XOR the selected ciphertext into the aggregate response, invalidate the selected slot, increment the bucket count, and persist the updated bucket file immediately.

Eviction and early reshuffle still use encrypted bucket reads and writes over the network. Client-side bucket fill reads eligible blocks from the disk-backed stash, removes selected blocks from that stash, and persists the reduced stash.

## Error Handling

The server fails initialization if any bucket has the wrong shape. It fails later requests if bucket files are missing, truncated, malformed, or out of range.

The client creates parent directories for its stash path. Missing stash files are treated as empty stash only before initialization has written the initial stash. Malformed stash payloads throw an exception.

## Testing

Focused tests will assert that client initialization creates one server bucket file per tree node and a nonempty client stash file, and that a write/read workload updates the stash file while preserving correctness.

Existing Ring ORAM network tests remain the behavioral regression suite for read path, eviction, early reshuffle, and overwrite behavior.
