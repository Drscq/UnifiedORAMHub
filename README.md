

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

## Contributing
We welcome contributions to UnifiedORAMHub! Please read our [CONTRIBUTING.md](/CONTRIBUTING.md) for guidelines on how to contribute.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments
- TFHE Library: https://github.com/tfhe/tfhe
- PathORAM and Onion ORM research communities.
