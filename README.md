

# UnifiedORAMHub

## Overview
UnifiedORAMHub is a modular and standardized platform for implementing and comparing various Oblivious Random Access Machine (ORAM) schemes. This project aims to accelerate research and development in the field of privacy-preserving technologies by providing a common ground for researchers and developers to analyze, improve, and benchmark different ORAM algorithms.

## Features
- **Standardized API**: Ensures consistency across different ORAM implementations.
- **Modular Architecture**: Facilitates easy addition and comparison of various ORAM schemes.
- **Comprehensive Toolkit**: Provides essential tools and functions common across ORAM implementations.
- **Community-Driven**: Open for contributions and collaborative enhancements.

## Getting Started

### Prerequisites
- **CMake** (>= 3.14)
- **C++ Compiler** (supporting C++17)
- **OpenSSL** (development headers/libraries)
- **TFHE** (automatically fetched during build)
- **GoogleTest** (automatically fetched during build)

### Building
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Running Tests
The project uses `ctest` to manage tests. To run all tests:
```bash
cd build
ctest --output-on-failure
```

#### Onion Ring ORAM Tests
Specific tests for the Onion Ring ORAM scheme can be found in the `onion_ring_tests` executable. 

**Note**: Some tests involve network communication on local ports. If you encounter "Address already in use" errors when running tests in parallel, run them sequentially:
```bash
ctest --output-on-failure -j1
```

You can also run the Onion Ring test suite directly:
```bash
./test/onion_ring_tests
```

### ORAM Delay Comparison
Compare Path ORAM and Ring ORAM end-to-end network delay with the analytical helper:
```bash
python3 scripts/oram_delay_comparison.py
```

The default scenario uses `N = 2**16` blocks, block sizes `4`, `8`, and `16` KiB,
16 tree levels, 5 ms RTT latency, 50 Mbps bandwidth, and `10 * A = 480`
accesses for Ring ORAM's default `A = 48`.

Save the same results as CSV:
```bash
python3 scripts/oram_delay_comparison.py --format csv > oram_delay_results.csv
```

For a larger database, pass the new size and levels explicitly:
```bash
python3 scripts/oram_delay_comparison.py \
  --num-blocks 1048576 \
  --levels 20 \
  --block-sizes-kib 4 8 16 32 \
  --format csv
```

By default, Ring ORAM eviction is modeled as one batched path read RTT and one
batched path write RTT. To model a separate read/write RTT per bucket level, add:
```bash
--ring-eviction-rtt-mode bucket
```

### Manual Network Emulation with tc
Use the `tc` helper when you want the real client/server communication to run
under the same 5 ms RTT and 50 Mbps network limit:
```bash
scripts/tc_network_limit.sh apply --dry-run
sudo scripts/tc_network_limit.sh apply --dev lo
```

The default interface is `lo`, bandwidth is `50mbit`, and one-way delay is
`2.5ms`, which approximates a 5 ms RTT for local client/server runs over
loopback. For a physical or virtual NIC, pass the interface explicitly:
```bash
sudo scripts/tc_network_limit.sh apply --dev eth0 --rtt-ms 5 --bandwidth 50mbit
```

Inspect or remove the limiter:
```bash
scripts/tc_network_limit.sh show --dev lo
sudo scripts/tc_network_limit.sh clear --dev lo
```

`tc` shapes all egress traffic on the selected interface. Clear the qdisc when
the experiment is finished.

After applying the limiter, run the TCP end-to-end comparison that sends the
Path ORAM and Ring ORAM payloads from the specified protocol model:
```bash
python3 scripts/oram_tcp_benchmark.py
```

This benchmark does not allocate the full database. It uses `N = 2**16` only as
scenario metadata, sends the exact per-access and eviction payload sizes for
4/8/16 KiB blocks, and measures 480 accesses over TCP. To scale the same harness:
```bash
python3 scripts/oram_tcp_benchmark.py \
  --num-blocks 1048576 \
  --levels 20 \
  --block-sizes-kib 4 8 16 32 \
  --accesses 480 \
  --format csv
```

For the real C++ comparison with client computation, server-side generated path
storage, AES-CTR encryption/decryption, and TCP communication under the active
`tc` limiter, run:
```bash
python3 scripts/oram_real_delay_benchmark.py
```

This benchmark does not materialize the whole binary tree. It generates only the
path data needed for each measured access, defaults to 480 accesses, and reports
Path ORAM read/write path time against Ring ORAM online reads plus amortized
eviction every `A = 48` accesses. Forward options to the C++ executable after
`--`:
```bash
python3 scripts/oram_real_delay_benchmark.py -- \
  --block-sizes-kib 4 8 16 \
  --accesses 480 \
  --format csv
```

## Contributing
We welcome contributions to UnifiedORAMHub! Please read our [CONTRIBUTING.md](/CONTRIBUTING.md) for guidelines on how to contribute.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments
- TFHE Library: https://github.com/tfhe/tfhe
- PathORAM and Onion ORM research communities.
