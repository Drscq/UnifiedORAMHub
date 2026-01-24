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

# Library target - Core ORAM components
add_library(oram_core
    src/core/Block.cpp
    src/core/Bucket.cpp
    src/network/NetIO.cpp
)

# We need to set the sources property if it's empty to avoid CMake errors, 
# or we can just dummy it for now until we have source files. 
# But usually add_library without sources is valid if we add them later or if it's an interface lib.
# However, we planned to have sources. Let's make it an INTERFACE library for now if we don't have .cpp files immediately,
# OR we can just wait to add it until we create the files.
# actually, let's start with Block.h (header only potentially?) or just a dummy source file.
# The plan said Block.h, etc.
# Check the plan: "Block.h - Defines the fundamental unit... Methods for serialization". Likely template or header-only for now, or small .cpp.

# Let's adjust: We will add sources as we create them. 
# For now, I'll put a placeholder or just wait on the add_library call until I create the files?
# No, I should create the CMakeLists.txt. I'll include a dummy source file or just Block.cpp if I make one.
# Re-reading the plan: "Methods for serialization/deserialization".
# I'll Assume we will have a src/core/Block.cpp eventually. 
# For this step, I'll stick to the plan of creating the build files.

# Let's define the target but maybe COMMENT OUT the source until the file exists to avoid immediate configure error?
# Or better: Create the file alongside.

# USEFUL PATTERN: I'll create the file content for CMakeLists.txt but with the expectation that I'm about to create the source files.

target_include_directories(oram_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Tests
enable_testing()
add_subdirectory(test)
