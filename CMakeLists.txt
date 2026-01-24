cmake_minimum_required(VERSION 3.14)
project(UnifiedORAMHub VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

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

# Tests
enable_testing()
add_subdirectory(test)
