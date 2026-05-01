#!/usr/bin/env python3
"""TCP end-to-end payload benchmark for Path ORAM and Ring ORAM traffic."""

import argparse
import csv
import json
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from oram_delay_comparison import (
    BYTES_PER_KIB,
    BYTES_PER_MIB,
    NetworkConfig,
    PathORAMConfig,
    RingORAMConfig,
    Scenario,
    compare_scenario,
    default_scenario,
    positive_float,
    positive_int,
)


HEADER = struct.Struct("!BQ")
CMD_READ = 1
CMD_WRITE = 2
CMD_QUIT = 3
ACK = b"\xAC"
CHUNK_SIZE = 64 * 1024
ZERO_CHUNK = b"\0" * CHUNK_SIZE


@dataclass(frozen=True)
class PathPayloadPlan:
    read_bytes: int
    write_bytes: int


@dataclass(frozen=True)
class RingPayloadPlan:
    online_read_bytes: int
    eviction_read_bytes: int
    eviction_write_bytes: int


@dataclass(frozen=True)
class MeasuredResult:
    algorithm: str
    block_size_kib: int
    accesses: int
    evictions: int
    transfer_bytes: int
    elapsed_seconds: float
    expected_seconds: float
    expected_ms_per_access: float

    @property
    def measured_ms_per_access(self) -> float:
        return self.elapsed_seconds * 1000.0 / self.accesses

    @property
    def transfer_mib(self) -> float:
        return self.transfer_bytes / BYTES_PER_MIB


def path_payload_plan(block_size_bytes: int, levels: int, z: int) -> PathPayloadPlan:
    require_positive("block_size_bytes", block_size_bytes)
    require_positive("levels", levels)
    require_positive("z", z)
    path_bytes = levels * z * block_size_bytes
    return PathPayloadPlan(read_bytes=path_bytes, write_bytes=path_bytes)


def ring_payload_plan(block_size_bytes: int, levels: int, z: int, s: int) -> RingPayloadPlan:
    require_positive("block_size_bytes", block_size_bytes)
    require_positive("levels", levels)
    require_positive("z", z)
    require_positive("s", s)
    return RingPayloadPlan(
        online_read_bytes=block_size_bytes,
        eviction_read_bytes=levels * z * block_size_bytes,
        eviction_write_bytes=levels * (z + s) * block_size_bytes,
    )


def require_positive(name: str, value: int | float) -> None:
    if value <= 0:
        raise ValueError(f"{name} must be positive")


class PayloadServer:
    def __init__(self, host: str, port: int):
        self.host = host
        self._requested_port = port
        self.port = port
        self._ready = threading.Event()
        self._stopped = threading.Event()
        self._thread: threading.Thread | None = None
        self._listener: socket.socket | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._serve, name="oram-payload-server", daemon=True)
        self._thread.start()
        if not self._ready.wait(timeout=5):
            raise RuntimeError("payload server did not start")

    def stop(self) -> None:
        if self._stopped.is_set():
            return
        try:
            with socket.create_connection((self.host, self.port), timeout=5) as conn:
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                conn.sendall(HEADER.pack(CMD_QUIT, 0))
        except OSError:
            pass
        if self._thread is not None:
            self._thread.join(timeout=5)
        self._stopped.set()

    def _serve(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((self.host, self._requested_port))
            listener.listen()
            self._listener = listener
            self.port = listener.getsockname()[1]
            self._ready.set()
            while True:
                conn, _ = listener.accept()
                with conn:
                    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    if self._handle_connection(conn):
                        return

    def _handle_connection(self, conn: socket.socket) -> bool:
        while True:
            header = recv_exact_or_empty(conn, HEADER.size)
            if not header:
                return False
            command, size = HEADER.unpack(header)
            if command == CMD_READ:
                send_zeroes(conn, size)
            elif command == CMD_WRITE:
                recv_discard(conn, size)
                conn.sendall(ACK)
            elif command == CMD_QUIT:
                return True
            else:
                raise RuntimeError(f"unknown command {command}")


class PayloadClient:
    def __init__(self, host: str, port: int):
        self._conn = socket.create_connection((host, port), timeout=10)
        self._conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self) -> None:
        self._conn.close()

    def request_read(self, size: int) -> None:
        self._conn.sendall(HEADER.pack(CMD_READ, size))
        recv_discard(self._conn, size)

    def request_write(self, size: int) -> None:
        self._conn.sendall(HEADER.pack(CMD_WRITE, size))
        send_zeroes(self._conn, size)
        ack = recv_exact_or_empty(self._conn, len(ACK))
        if ack != ACK:
            raise RuntimeError("missing server acknowledgment")


def recv_exact_or_empty(conn: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = conn.recv(min(CHUNK_SIZE, remaining))
        if not chunk:
            if remaining == size:
                return b""
            raise RuntimeError("connection closed while receiving payload")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def recv_discard(conn: socket.socket, size: int) -> None:
    remaining = size
    while remaining > 0:
        chunk = conn.recv(min(CHUNK_SIZE, remaining))
        if not chunk:
            raise RuntimeError("connection closed while receiving payload")
        remaining -= len(chunk)


def send_zeroes(conn: socket.socket, size: int) -> None:
    remaining = size
    while remaining > 0:
        chunk_size = min(CHUNK_SIZE, remaining)
        conn.sendall(ZERO_CHUNK[:chunk_size])
        remaining -= chunk_size


def measure_path_oram(
    *,
    host: str,
    port: int,
    block_size_bytes: int,
    accesses: int,
    levels: int,
    z: int,
) -> MeasuredResult:
    plan = path_payload_plan(block_size_bytes, levels, z)
    client = PayloadClient(host, port)
    start = time.perf_counter()
    try:
        for _ in range(accesses):
            client.request_read(plan.read_bytes)
            client.request_write(plan.write_bytes)
    finally:
        elapsed = time.perf_counter() - start
        client.close()

    transfer_bytes = accesses * (plan.read_bytes + plan.write_bytes)
    return MeasuredResult(
        algorithm="Path ORAM",
        block_size_kib=block_size_bytes // BYTES_PER_KIB,
        accesses=accesses,
        evictions=0,
        transfer_bytes=transfer_bytes,
        elapsed_seconds=elapsed,
        expected_seconds=0.0,
        expected_ms_per_access=0.0,
    )


def measure_ring_oram(
    *,
    host: str,
    port: int,
    block_size_bytes: int,
    accesses: int,
    levels: int,
    z: int,
    s: int,
    a: int,
) -> MeasuredResult:
    plan = ring_payload_plan(block_size_bytes, levels, z, s)
    client = PayloadClient(host, port)
    evictions = 0
    transfer_bytes = 0
    start = time.perf_counter()
    try:
        for access_index in range(1, accesses + 1):
            client.request_read(plan.online_read_bytes)
            transfer_bytes += plan.online_read_bytes
            if access_index % a == 0:
                evictions += 1
                client.request_read(plan.eviction_read_bytes)
                client.request_write(plan.eviction_write_bytes)
                transfer_bytes += plan.eviction_read_bytes + plan.eviction_write_bytes
    finally:
        elapsed = time.perf_counter() - start
        client.close()

    return MeasuredResult(
        algorithm="Ring ORAM",
        block_size_kib=block_size_bytes // BYTES_PER_KIB,
        accesses=accesses,
        evictions=evictions,
        transfer_bytes=transfer_bytes,
        elapsed_seconds=elapsed,
        expected_seconds=0.0,
        expected_ms_per_access=0.0,
    )


def attach_expected_results(results: list[MeasuredResult], scenario: Scenario) -> list[MeasuredResult]:
    expected_by_key = {}
    for path_expected, ring_expected in compare_scenario(scenario, eviction_rtt_mode="path"):
        expected_by_key[("Path ORAM", path_expected.block_size_kib)] = path_expected
        expected_by_key[("Ring ORAM", ring_expected.block_size_kib)] = ring_expected

    attached = []
    for result in results:
        expected = expected_by_key[(result.algorithm, result.block_size_kib)]
        attached.append(
            MeasuredResult(
                algorithm=result.algorithm,
                block_size_kib=result.block_size_kib,
                accesses=result.accesses,
                evictions=result.evictions,
                transfer_bytes=result.transfer_bytes,
                elapsed_seconds=result.elapsed_seconds,
                expected_seconds=expected.total_seconds,
                expected_ms_per_access=expected.amortized_ms,
            )
        )
    return attached


def run_benchmark(scenario: Scenario, host: str, port: int, algorithms: tuple[str, ...]) -> list[MeasuredResult]:
    server = PayloadServer(host, port)
    server.start()
    results: list[MeasuredResult] = []
    try:
        for block_size_kib in scenario.block_sizes_kib:
            block_size_bytes = block_size_kib * BYTES_PER_KIB
            if "path" in algorithms:
                print(f"Measuring Path ORAM, B={block_size_kib} KiB...", file=sys.stderr, flush=True)
                results.append(
                    measure_path_oram(
                        host=host,
                        port=server.port,
                        block_size_bytes=block_size_bytes,
                        accesses=scenario.accesses,
                        levels=scenario.path.levels,
                        z=scenario.path.z,
                    )
                )
            if "ring" in algorithms:
                print(f"Measuring Ring ORAM, B={block_size_kib} KiB...", file=sys.stderr, flush=True)
                results.append(
                    measure_ring_oram(
                        host=host,
                        port=server.port,
                        block_size_bytes=block_size_bytes,
                        accesses=scenario.accesses,
                        levels=scenario.ring.levels,
                        z=scenario.ring.z,
                        s=scenario.ring.s,
                        a=scenario.ring.a,
                    )
                )
    finally:
        server.stop()
    return attach_expected_results(results, scenario)


def result_rows(results: list[MeasuredResult]) -> list[dict[str, object]]:
    path_elapsed_by_block = {
        result.block_size_kib: result.elapsed_seconds
        for result in results
        if result.algorithm == "Path ORAM"
    }
    rows = []
    for result in results:
        measured_speedup = ""
        if result.algorithm == "Ring ORAM" and result.block_size_kib in path_elapsed_by_block:
            measured_speedup = f"{path_elapsed_by_block[result.block_size_kib] / result.elapsed_seconds:.3f}"
        rows.append(
            {
                "algorithm": result.algorithm,
                "block_size_kib": result.block_size_kib,
                "accesses": result.accesses,
                "evictions": result.evictions,
                "transfer_mib": f"{result.transfer_mib:.6f}",
                "measured_seconds": f"{result.elapsed_seconds:.6f}",
                "measured_ms_per_access": f"{result.measured_ms_per_access:.6f}",
                "expected_seconds": f"{result.expected_seconds:.6f}",
                "expected_ms_per_access": f"{result.expected_ms_per_access:.6f}",
                "measured_ring_speedup_vs_path": measured_speedup,
            }
        )
    return rows


def format_markdown(scenario: Scenario, rows: list[dict[str, object]]) -> str:
    lines = [
        "# ORAM TCP End-to-End Benchmark",
        "",
        f"N={scenario.num_blocks}, accesses={scenario.accesses}, block sizes={list(scenario.block_sizes_kib)} KiB",
        "",
        "| Algorithm | B (KiB) | Evictions | Transfer (MiB) | Measured (s) | Measured (ms/access) | Expected (s) | Ring speedup |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            "| {algorithm} | {block_size_kib} | {evictions} | {transfer_mib} | "
            "{measured_seconds} | {measured_ms_per_access} | {expected_seconds} | "
            "{measured_ring_speedup_vs_path} |".format(**row)
        )
    return "\n".join(lines)


def write_csv(rows: list[dict[str, object]], stream) -> None:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)


def build_parser() -> argparse.ArgumentParser:
    defaults = default_scenario()
    parser = argparse.ArgumentParser(
        description="Measure ORAM protocol payload delays over TCP, useful with tc shaping."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0, help="Server port; 0 chooses an ephemeral port.")
    parser.add_argument("--num-blocks", type=positive_int, default=defaults.num_blocks)
    parser.add_argument("--block-sizes-kib", type=positive_int, nargs="+", default=list(defaults.block_sizes_kib))
    parser.add_argument("--accesses", type=positive_int, default=None, help="Defaults to 10 * --ring-a.")
    parser.add_argument("--levels", type=positive_int, default=defaults.path.levels)
    parser.add_argument("--path-z", type=positive_int, default=defaults.path.z)
    parser.add_argument("--ring-z", type=positive_int, default=defaults.ring.z)
    parser.add_argument("--ring-a", type=positive_int, default=defaults.ring.a)
    parser.add_argument("--ring-s", type=positive_int, default=defaults.ring.s)
    parser.add_argument("--latency-ms", type=positive_float, default=defaults.network.latency_ms)
    parser.add_argument("--bandwidth-mbps", type=positive_float, default=defaults.network.bandwidth_mbps)
    parser.add_argument("--algorithms", choices=("path", "ring"), nargs="+", default=["path", "ring"])
    parser.add_argument("--format", choices=("markdown", "csv", "json"), default="markdown")
    return parser


def scenario_from_args(args: argparse.Namespace) -> Scenario:
    accesses = args.accesses if args.accesses is not None else 10 * args.ring_a
    return Scenario(
        num_blocks=args.num_blocks,
        block_sizes_kib=tuple(args.block_sizes_kib),
        accesses=accesses,
        network=NetworkConfig(latency_ms=args.latency_ms, bandwidth_mbps=args.bandwidth_mbps),
        path=PathORAMConfig(levels=args.levels, z=args.path_z),
        ring=RingORAMConfig(levels=args.levels, z=args.ring_z, a=args.ring_a, s=args.ring_s),
    )


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    scenario = scenario_from_args(args)
    results = run_benchmark(scenario, args.host, args.port, tuple(args.algorithms))
    rows = result_rows(results)
    if args.format == "markdown":
        print(format_markdown(scenario, rows))
    elif args.format == "csv":
        write_csv(rows, sys.stdout)
    else:
        print(json.dumps(rows, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
