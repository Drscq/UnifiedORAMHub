include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# We will define test executables here
add_executable(oram_tests
    core/block_test.cpp
    core/bucket_test.cpp
    core/ram_test.cpp
    network/net_io_test.cpp
)
target_link_libraries(oram_tests PRIVATE
    oram_core
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

gtest_discover_tests(oram_tests)
