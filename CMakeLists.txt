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
FetchContent_MakeAvailable(tfhe)

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
)

target_include_directories(oram_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(oram_core PUBLIC
    OpenSSL::Crypto
)

add_library(onion_ring_core
    src/onion_ring/TFHEAdapter.cpp
    src/onion_ring/HomOps.cpp
    src/onion_ring/WaksmanNetwork.cpp
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
