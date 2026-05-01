#!/usr/bin/env python3
"""Build and run the real C++ ORAM delay benchmark."""

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TARGET = "oram_real_delay_benchmark"


def command_to_string(command: list[str]) -> str:
    return " ".join(command)


def build_commands(build_dir: Path) -> list[list[str]]:
    commands: list[list[str]] = []
    if not (build_dir / "CMakeCache.txt").exists():
        commands.append(["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir)])
    commands.append(["cmake", "--build", str(build_dir), "--target", TARGET, "-j2"])
    return commands


def benchmark_path(build_dir: Path) -> Path:
    return build_dir / TARGET


def parse_args(argv: list[str]) -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(
        description="Build and run the real C++ Path ORAM vs Ring ORAM delay benchmark."
    )
    parser.add_argument("--build-dir", default=str(REPO_ROOT / "build"))
    parser.add_argument("--no-build", action="store_true", help="skip the CMake build step")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running")
    return parser.parse_known_args(argv)


def main(argv: list[str] | None = None) -> int:
    args, benchmark_args = parse_args(sys.argv[1:] if argv is None else argv)
    if benchmark_args and benchmark_args[0] == "--":
        benchmark_args = benchmark_args[1:]
    build_dir = Path(args.build_dir).resolve()

    commands: list[list[str]] = []
    if not args.no_build:
        commands.extend(build_commands(build_dir))
    commands.append([str(benchmark_path(build_dir)), *benchmark_args])

    if args.dry_run:
        for command in commands:
            print(command_to_string(command))
        return 0

    for command in commands:
        subprocess.run(command, cwd=REPO_ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
