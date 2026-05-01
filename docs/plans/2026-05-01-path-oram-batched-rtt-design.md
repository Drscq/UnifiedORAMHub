# Path ORAM Batched RTT Design

**Date:** 2026-05-01

## Goal

Make each Path ORAM access use one read-path request/response and one write-path request/response, matching the intended end-to-end delay model for Path ORAM while preserving the existing real client/server computation.

## Current State

`PathORAMClient::ReadPath()` reads every bucket on a path by calling `ReadBucketFromServer()` once per bucket. `WritePath()` similarly calls `WriteBucketToServer()` once per bucket. For a 16-level path, the current wire protocol creates 16 read request/response exchanges plus 16 write sends, so the measured delay includes per-bucket network turnarounds instead of the desired path-level read RTT and write RTT.

## Architecture

The recommended approach is to keep the existing bucket encryption, decryption, serialization, stash processing, and disk-backed server storage, but add path-level wire commands to `PathORAMClient` and `PathORAMServer`.

The client sends one batched read-path command containing the path bucket indices. The server reads each bucket from disk, encrypts each bucket using the existing AES-CTR helper, and returns all encrypted buckets in one response. The client decrypts each bucket and adds real blocks to the stash.

For write-back, the client evicts blocks into the path buckets as it already does, encrypts each bucket, sends all bucket indices and encrypted payloads in one batched write-path command, and waits for a one-byte server acknowledgement after the server decrypts and stores the path. That acknowledgement makes the write path a real request/response boundary under `tc`.

The existing single-bucket `R` and `W` handlers can remain in place for compatibility, but normal `Access()` should use only the path-level commands.

## Data Flow

On read, the client sends `path_count` and the bucket indices for the target leaf. The server replies with the same count followed by size-prefixed encrypted bucket payloads in path order.

On write, the client sends `path_count`, then for each bucket sends the bucket index and size-prefixed encrypted bucket payload. The server validates/decrypts/stores every bucket, then sends an acknowledgement byte.

## Error Handling

The server continues to reject uninitialized storage, out-of-range bucket indices, malformed encrypted payloads, and invalid bucket shapes. The batched protocol validates the path count and raises if the server replies with an unexpected count or acknowledgement.

## Testing

Add a focused Path ORAM protocol-shape test that runs a normal client write through a real server and asserts the server observed one batched read-path command, one batched write-path command, and zero single-bucket read/write commands for that access. Existing correctness tests remain the regression suite for stash behavior and disk-backed storage.
