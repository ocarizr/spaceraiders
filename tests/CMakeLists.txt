add_executable(vector_tests core/vector_tests.cpp)
target_include_directories(vector_tests PRIVATE "${PROJECT_SOURCE_DIR}")
set_target_properties(vector_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

if(MSVC)
    target_compile_options(vector_tests PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vector_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME core.vector_coordinates COMMAND vector_tests)
set_tests_properties(core.vector_coordinates PROPERTIES TIMEOUT 10)
