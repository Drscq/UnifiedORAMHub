---
name: cpp-project-skill
description: Scaffold and iteratively develop a modern C++ project with CMake, GoogleTest, and a strict TDD workflow.
---

# C++ Project Development Skill

This skill guides an agent through setting up and maintaining a **production-grade C++ project** using modern CMake, GoogleTest, and a strict Test-Driven Development workflow.

---

## 1. Project Initialization

When starting a new C++ project, create the following directory structure:

```
<project_name>/
├── CMakeLists.txt
├── cmake/
├── include/<project_name>/
├── src/
├── test/
│   └── CMakeLists.txt
├── .clang-format
└── .gitignore
```

### 1.1 Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.14)
project(<PROJECT_NAME> VERSION 1.0.0 LANGUAGES CXX)

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

# Library target
add_library(${PROJECT_NAME}_lib
    src/example.cpp
)
target_include_directories(${PROJECT_NAME}_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Executable (optional)
add_executable(${PROJECT_NAME}_app src/main.cpp)
target_link_libraries(${PROJECT_NAME}_app PRIVATE ${PROJECT_NAME}_lib)

# Tests
enable_testing()
add_subdirectory(test)
```

### 1.2 test/CMakeLists.txt

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(${PROJECT_NAME}_tests
    example_test.cpp
)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)

gtest_discover_tests(${PROJECT_NAME}_tests)
```

### 1.3 .clang-format

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
```

### 1.4 .gitignore

```
build/
cmake-build-*/
*.o
*.a
*.so
CMakeCache.txt
CMakeFiles/
compile_commands.json
.cache/
```

---

## 2. Development Workflow (Strict TDD)

> [!IMPORTANT]
> **Every code change MUST be accompanied by a unit test.**
> **Never make large changes in a single step.**

Follow this cycle for every feature or bug fix:

### Step 1: Write a Failing Test
Create or update a test file in `test/` that describes the expected behavior.

```cpp
// test/example_test.cpp
#include <gtest/gtest.h>
#include "<project_name>/example.h"

TEST(ExampleTest, AdditionWorks) {
    EXPECT_EQ(add(2, 3), 5);
}
```

### Step 2: Run Tests (Expect Failure)
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
The test should **fail** because the function doesn't exist yet.

### Step 3: Implement Minimal Code
Add only enough code in `src/` and `include/` to make the test pass.

```cpp
// include/<project_name>/example.h
#pragma once
int add(int a, int b);

// src/example.cpp
#include "<project_name>/example.h"
int add(int a, int b) { return a + b; }
```

### Step 4: Run Tests (Expect Pass)
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
All tests must pass before proceeding.

### Step 5: Refactor (Optional)
Clean up code while keeping tests green.

### Step 6: Commit & Push
Only after **all tests pass**, commit and push:
```bash
git add -A
git commit -m "feat: implement add function with tests"
git push origin main
```

---

## 3. Build & Test Commands

```bash
# Configure (once, from project root)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure

# Format code (optional)
find src include test -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

---

## 4. Git Workflow Rules

1. **Never push code that fails tests.**
2. Commit messages should reference the test/feature being added.
3. Keep commits small and focused (one logical change per commit).
4. Squash WIP commits before merging to main.

---

## 5. (Optional) GitHub Actions CI

Create `.github/workflows/ci.yml`:

```yaml
name: C++ CI

on: [push, pull_request]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

---

## 6. Checklist for Agent

When working on a C++ project with this skill:

- [ ] Verify project structure exists (or create it)
- [ ] For each change:
  - [ ] Write/update test first
  - [ ] Run tests (should fail)
  - [ ] Implement code
  - [ ] Run tests (must pass)
  - [ ] Commit with descriptive message
- [ ] Push only when all tests pass
