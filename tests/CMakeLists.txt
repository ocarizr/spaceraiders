add_executable(vector_tests core/vector_tests.cpp)
target_link_libraries(vector_tests PRIVATE GTest::gtest_main)
target_include_directories(vector_tests PRIVATE "${PROJECT_SOURCE_DIR}")
set_target_properties(
    vector_tests
    PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(vector_tests PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vector_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(vector_tests TEST_PREFIX "core." DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 10)
