#!/usr/bin/env python3
"""Compare Path ORAM and Ring ORAM end-to-end network delay."""

import argparse
import csv
import json
import math
import sys
from dataclasses import dataclass
from typing import Iterable


BITS_PER_BYTE = 8
BYTES_PER_KIB = 1024
BYTES_PER_MIB = 1024 * 1024
BITS_PER_MEGABIT = 1_000_000


@dataclass(frozen=True)
class NetworkConfig:
    latency_ms: float = 5.0
    bandwidth_mbps: float = 50.0

    def __post_init__(self) -> None:
        _require_positive("latency_ms", self.latency_ms)
        _require_positive("bandwidth_mbps", self.bandwidth_mbps)

    @property
    def latency_seconds(self) -> float:
        return self.latency_ms / 1000.0

    @property
    def bandwidth_bps(self) -> float:
        return self.bandwidth_mbps * BITS_PER_MEGABIT


@dataclass(frozen=True)
class PathORAMConfig:
    levels: int = 16
    z: int = 4

    def __post_init__(self) -> None:
        _require_positive("path levels", self.levels)
        _require_positive("path z", self.z)


@dataclass(frozen=True)
class RingORAMConfig:
    levels: int = 16
    z: int = 33
    a: int = 48
    s: int = 48

    def __post_init__(self) -> None:
        _require_positive("ring levels", self.levels)
        _require_positive("ring z", self.z)
        _require_positive("ring a", self.a)
        _require_positive("ring s", self.s)


@dataclass(frozen=True)
class Scenario:
    num_blocks: int
    block_sizes_kib: tuple[int, ...]
    accesses: int
    network: NetworkConfig
    path: PathORAMConfig
    ring: RingORAMConfig

    def __post_init__(self) -> None:
        _require_positive("num_blocks", self.num_blocks)
        _require_positive("accesses", self.accesses)
        if not self.block_sizes_kib:
            raise ValueError("at least one block size is required")
        for block_size_kib in self.block_sizes_kib:
            _require_positive("block size KiB", block_size_kib)


@dataclass(frozen=True)
class DelayResult:
    algorithm: str
    block_size_bytes: int
    accesses: int
    evictions: int
    round_trips: int
    transfer_bits: int
    total_seconds: float

    @property
    def block_size_kib(self) -> int:
        return self.block_size_bytes // BYTES_PER_KIB

    @property
    def amortized_ms(self) -> float:
        return self.total_seconds * 1000.0 / self.accesses

    @property
    def transfer_mib(self) -> float:
        return self.transfer_bits / BITS_PER_BYTE / BYTES_PER_MIB


def _require_positive(name: str, value: float) -> None:
    if value <= 0:
        raise ValueError(f"{name} must be positive")


def default_scenario() -> Scenario:
    ring = RingORAMConfig()
    return Scenario(
        num_blocks=2**16,
        block_sizes_kib=(4, 8, 16),
        accesses=10 * ring.a,
        network=NetworkConfig(),
        path=PathORAMConfig(),
        ring=ring,
    )


def path_oram_delay(
    *,
    block_size_bytes: int,
    accesses: int,
    network: NetworkConfig,
    config: PathORAMConfig,
) -> DelayResult:
    _require_positive("block_size_bytes", block_size_bytes)
    _require_positive("accesses", accesses)

    block_bits = block_size_bytes * BITS_PER_BYTE
    bits_per_path = config.levels * config.z * block_bits
    transfer_bits = accesses * 2 * bits_per_path
    round_trips = accesses * 2
    total_seconds = round_trips * network.latency_seconds + transfer_bits / network.bandwidth_bps

    return DelayResult(
        algorithm="Path ORAM",
        block_size_bytes=block_size_bytes,
        accesses=accesses,
        evictions=0,
        round_trips=round_trips,
        transfer_bits=transfer_bits,
        total_seconds=total_seconds,
    )


def ring_oram_delay(
    *,
    block_size_bytes: int,
    accesses: int,
    network: NetworkConfig,
    config: RingORAMConfig,
    eviction_rtt_mode: str = "path",
) -> DelayResult:
    _require_positive("block_size_bytes", block_size_bytes)
    _require_positive("accesses", accesses)

    if eviction_rtt_mode == "path":
        round_trips_per_eviction = 2
    elif eviction_rtt_mode == "bucket":
        round_trips_per_eviction = 2 * config.levels
    else:
        raise ValueError("eviction_rtt_mode must be 'path' or 'bucket'")

    block_bits = block_size_bytes * BITS_PER_BYTE
    evictions = accesses // config.a
    online_transfer_bits = accesses * block_bits
    eviction_transfer_bits = evictions * config.levels * (config.z + config.z + config.s) * block_bits
    transfer_bits = online_transfer_bits + eviction_transfer_bits
    round_trips = accesses + evictions * round_trips_per_eviction
    total_seconds = round_trips * network.latency_seconds + transfer_bits / network.bandwidth_bps

    return DelayResult(
        algorithm="Ring ORAM",
        block_size_bytes=block_size_bytes,
        accesses=accesses,
        evictions=evictions,
        round_trips=round_trips,
        transfer_bits=transfer_bits,
        total_seconds=total_seconds,
    )


def compare_scenario(scenario: Scenario, eviction_rtt_mode: str = "path") -> list[tuple[DelayResult, DelayResult]]:
    results = []
    for block_size_kib in scenario.block_sizes_kib:
        block_size_bytes = block_size_kib * BYTES_PER_KIB
        path_result = path_oram_delay(
            block_size_bytes=block_size_bytes,
            accesses=scenario.accesses,
            network=scenario.network,
            config=scenario.path,
        )
        ring_result = ring_oram_delay(
            block_size_bytes=block_size_bytes,
            accesses=scenario.accesses,
            network=scenario.network,
            config=scenario.ring,
            eviction_rtt_mode=eviction_rtt_mode,
        )
        results.append((path_result, ring_result))
    return results


def result_rows(
    scenario: Scenario,
    comparison: Iterable[tuple[DelayResult, DelayResult]],
    eviction_rtt_mode: str,
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path_result, ring_result in comparison:
        speedup = path_result.total_seconds / ring_result.total_seconds
        rows.append(_result_row(scenario, path_result, eviction_rtt_mode, ""))
        rows.append(_result_row(scenario, ring_result, eviction_rtt_mode, f"{speedup:.3f}"))
    return rows


def _result_row(
    scenario: Scenario,
    result: DelayResult,
    eviction_rtt_mode: str,
    speedup_vs_path: str,
) -> dict[str, object]:
    return {
        "algorithm": result.algorithm,
        "num_blocks": scenario.num_blocks,
        "block_size_kib": result.block_size_kib,
        "accesses": result.accesses,
        "evictions": result.evictions,
        "round_trips": result.round_trips,
        "transfer_mib": f"{result.transfer_mib:.6f}",
        "total_seconds": f"{result.total_seconds:.6f}",
        "amortized_ms": f"{result.amortized_ms:.6f}",
        "ring_eviction_rtt_mode": eviction_rtt_mode,
        "ring_speedup_vs_path": speedup_vs_path,
    }


def format_markdown(scenario: Scenario, rows: list[dict[str, object]]) -> str:
    lines = [
        "# ORAM Delay Comparison",
        "",
        (
            f"N={scenario.num_blocks} blocks, accesses={scenario.accesses}, "
            f"latency={scenario.network.latency_ms:g} ms RTT, "
            f"bandwidth={scenario.network.bandwidth_mbps:g} Mbps"
        ),
        "",
        "| Algorithm | Block (KiB) | Evictions | RTTs | Transfer (MiB) | Total (s) | Avg (ms/access) | Ring speedup vs Path |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            "| {algorithm} | {block_size_kib} | {evictions} | {round_trips} | "
            "{transfer_mib} | {total_seconds} | {amortized_ms} | {ring_speedup_vs_path} |".format(
                **row
            )
        )
    return "\n".join(lines)


def write_csv(rows: list[dict[str, object]], stream) -> None:
    fieldnames = [
        "algorithm",
        "num_blocks",
        "block_size_kib",
        "accesses",
        "evictions",
        "round_trips",
        "transfer_mib",
        "total_seconds",
        "amortized_ms",
        "ring_eviction_rtt_mode",
        "ring_speedup_vs_path",
    ]
    writer = csv.DictWriter(stream, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)


def format_json(rows: list[dict[str, object]]) -> str:
    return json.dumps(rows, indent=2)


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def ceil_log2(value: int) -> int:
    _require_positive("value", value)
    return math.ceil(math.log2(value))


def build_parser() -> argparse.ArgumentParser:
    defaults = default_scenario()
    parser = argparse.ArgumentParser(
        description="Analytically compare Path ORAM and Ring ORAM network delay."
    )
    parser.add_argument("--num-blocks", type=positive_int, default=defaults.num_blocks)
    parser.add_argument(
        "--block-sizes-kib",
        type=positive_int,
        nargs="+",
        default=list(defaults.block_sizes_kib),
        help="Block sizes to evaluate in KiB.",
    )
    parser.add_argument(
        "--accesses",
        type=positive_int,
        default=None,
        help="Number of accesses. Defaults to 10 * --ring-a.",
    )
    parser.add_argument("--levels", type=positive_int, default=defaults.path.levels)
    parser.add_argument(
        "--infer-levels-from-n",
        action="store_true",
        help="Use ceil(log2(num_blocks)) for both ORAM level counts.",
    )
    parser.add_argument("--latency-ms", type=positive_float, default=defaults.network.latency_ms)
    parser.add_argument(
        "--bandwidth-mbps", type=positive_float, default=defaults.network.bandwidth_mbps
    )
    parser.add_argument("--path-z", type=positive_int, default=defaults.path.z)
    parser.add_argument("--ring-z", type=positive_int, default=defaults.ring.z)
    parser.add_argument("--ring-a", type=positive_int, default=defaults.ring.a)
    parser.add_argument("--ring-s", type=positive_int, default=defaults.ring.s)
    parser.add_argument(
        "--ring-eviction-rtt-mode",
        choices=("path", "bucket"),
        default="path",
        help="'path' models 2 RTTs per eviction; 'bucket' models 2 RTTs per level.",
    )
    parser.add_argument("--format", choices=("markdown", "csv", "json"), default="markdown")
    return parser


def scenario_from_args(args: argparse.Namespace) -> Scenario:
    levels = ceil_log2(args.num_blocks) if args.infer_levels_from_n else args.levels
    accesses = args.accesses if args.accesses is not None else 10 * args.ring_a
    return Scenario(
        num_blocks=args.num_blocks,
        block_sizes_kib=tuple(args.block_sizes_kib),
        accesses=accesses,
        network=NetworkConfig(latency_ms=args.latency_ms, bandwidth_mbps=args.bandwidth_mbps),
        path=PathORAMConfig(levels=levels, z=args.path_z),
        ring=RingORAMConfig(levels=levels, z=args.ring_z, a=args.ring_a, s=args.ring_s),
    )


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    scenario = scenario_from_args(args)
    comparison = compare_scenario(scenario, eviction_rtt_mode=args.ring_eviction_rtt_mode)
    rows = result_rows(scenario, comparison, args.ring_eviction_rtt_mode)

    if args.format == "markdown":
        print(format_markdown(scenario, rows))
    elif args.format == "csv":
        write_csv(rows, sys.stdout)
    else:
        print(format_json(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
