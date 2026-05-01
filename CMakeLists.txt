cmake_minimum_required(VERSION 3.14)
project(UnifiedORAMHub VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

FetchContent_Declare(
    tfhe
    GIT_REPOSITORY https://github.com/tfhe/tfhe.git
    GIT_TAG        v1.0.1
    SOURCE_SUBDIR  src
)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
# Disable the nayuki FFT backends: fft_processor_nayuki.cpp uses `1i` (a GNU
# C imaginary suffix) which GCC 14 rejects in C++17 mode.  The spqlios
# backend is always preferred on x86 anyway, so nayuki is dead code for us.
set(ENABLE_NAYUKI_PORTABLE OFF CACHE BOOL "" FORCE)
set(ENABLE_NAYUKI_AVX      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tfhe)


# fft_processor_nayuki.cpp uses the `1i` complex imaginary literal which is
# a GNU extension.  GCC 14 in strict C++17 mode rejects it with "unable to
# find numeric literal operator 'operator""i'".  We fix this by explicitly
# overriding the language standard to gnu++17 for the two nayuki targets.
# The -std=gnu++17 flag appended last wins over the earlier -std=c++17.
foreach(nayuki_target tfhe-fft-nayuki-portable tfhe-fft-nayuki-avx)
    if(TARGET ${nayuki_target})
        target_compile_options(${nayuki_target} PRIVATE -std=gnu++17)
    endif()
endforeach()

foreach(tfhe_target
        tfhe-core
        tfhe-fft-nayuki-portable
        tfhe-fft-nayuki-avx
        tfhe-fft-spqlios-avx
        tfhe-fft-spqlios-fma
        tfhe-nayuki-portable
        tfhe-nayuki-avx
        tfhe-spqlios-avx
        tfhe-spqlios-fma)
    if(TARGET ${tfhe_target})
        target_compile_options(${tfhe_target} PRIVATE
            -Wno-error
            -fno-sanitize=undefined
            -fno-sanitize=signed-integer-overflow
        )
        target_link_options(${tfhe_target} PRIVATE
            -fno-sanitize=undefined
            -fno-sanitize=signed-integer-overflow
        )
    endif()
endforeach()

if(TARGET tfhe-spqlios-fma)
    set(ONION_RING_TFHE_TARGET tfhe-spqlios-fma)
elseif(TARGET tfhe-spqlios-avx)
    set(ONION_RING_TFHE_TARGET tfhe-spqlios-avx)
else()
    set(ONION_RING_TFHE_TARGET tfhe-nayuki-portable)
endif()

# Compiler warnings for this project only. These are intentionally added
# after FetchContent_MakeAvailable(tfhe) so the external TFHE build does not
# inherit our sanitizer policy.
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# Library target - Core ORAM components
add_library(oram_core
    src/core/Block.cpp
    src/core/Bucket.cpp
    src/network/NetIO.cpp
    src/crypto/AES_CTR.cpp
    src/path_oram/PathORAMServer.cpp
    src/path_oram/PathORAMClient.cpp
    src/ring_oram/RingBucket.cpp
    src/ring_oram/RingORAMServer.cpp
    src/ring_oram/RingORAMClient.cpp
)

target_include_directories(oram_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(oram_core PUBLIC
    OpenSSL::Crypto
)

add_library(oram_benchmark
    src/benchmark/ORAMDelayBenchmark.cpp
)
target_link_libraries(oram_benchmark PUBLIC
    oram_core
)

add_executable(oram_real_delay_benchmark
    benchmarks/oram_real_delay_benchmark.cpp
)
target_link_libraries(oram_real_delay_benchmark PRIVATE
    oram_benchmark
)

add_library(onion_ring_core
    src/onion_ring/TFHEAdapter.cpp
    src/onion_ring/HomOps.cpp
    src/onion_ring/WaksmanNetwork.cpp
    src/onion_ring/OnionRingServer.cpp
    src/onion_ring/OnionRingClient.cpp
)
target_include_directories(onion_ring_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${tfhe_SOURCE_DIR}/src/include
)
target_link_libraries(onion_ring_core PUBLIC
    oram_core
    OpenSSL::Crypto
    ${ONION_RING_TFHE_TARGET}
)

# Tests
enable_testing()
add_subdirectory(test)
