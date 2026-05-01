# Disk-Backed Path ORAM Design

**Date:** 2026-05-01

## Goal

Move Path ORAM's server-side bucket tree from process-only memory to per-run disk files while preserving the existing client/server protocol and public client behavior.

## Current State

`PathORAMServer` currently owns `std::vector<core::Bucket> tree_`. `Init()` fills that vector, `HandleReadBucket()` encrypts `tree_[bucket_index]`, and `HandleWriteBucket()` decrypts the client payload back into that vector. No bucket data is written to disk.

## Alternatives Considered

The recommended approach is a lightweight per-bucket file store: one serialized plaintext bucket file per tree node under a server storage directory. This mirrors the existing disk-backed Ring ORAM server pattern, keeps random bucket rewrites simple, and avoids adding a storage dependency.

A single flat database file was considered, but it would require fixed-size records or an index. SQLite/RocksDB would provide durable key-value semantics, but that is unnecessary build and runtime complexity for the current protocol.

## Architecture

`PathORAMServer` gains an optional `storage_dir` constructor argument. When empty, it derives a per-server path under the system temp directory from address and port. Existing callers keep working because the new argument has a default value.

The server no longer keeps the bucket tree as a member vector. `Init()` creates the tree directory, builds each bucket, and writes `bucket_<index>.bin` files. Read and write protocol handlers load or replace the requested bucket file on demand, encrypting/decrypting at the existing network boundary.

The client position map and stash remain client memory state. They are not the outsourced database that this change targets.

## Data Flow

During initialization, the server creates all bucket files and fills each bucket with any initial leaf block plus dummy blocks. On read, it loads the requested bucket file, encrypts it, and sends it to the client. On write, it receives an encrypted bucket, decrypts it, validates its shape, and truncates/replaces that bucket file immediately.

## Error Handling

The server rejects out-of-range bucket indices, missing or malformed bucket files, and bucket objects whose capacity or block count exceeds the configured Path ORAM bucket size.

## Testing

Focused tests configure an explicit temporary `storage_dir`, run a normal client/server workload, and assert that the expected tree files exist on disk and remain usable through read/write access. The existing Path ORAM tests remain the behavioral regression suite.
