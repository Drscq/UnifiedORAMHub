include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(MathLibTests math_tests.cpp)
target_link_libraries(MathLibTests PRIVATE
    MathLib
    GTest::gtest_main
)

gtest_discover_tests(MathLibTests)
