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

foreach(
    test_source
    IN
    ITEMS
        runtime/legacy_lifetime_tests.cpp
        runtime/legacy_time_tests.cpp
        game/legacy_rules_tests.cpp
)
    get_filename_component(runtime_test "${test_source}" NAME_WE)
    get_filename_component(test_family "${test_source}" DIRECTORY)
    add_executable(
        ${runtime_test}
        "${test_source}"
        "${PROJECT_SOURCE_DIR}/PlayField.cpp"
        "${PROJECT_SOURCE_DIR}/PowerUp.cpp"
        "${PROJECT_SOURCE_DIR}/renderer.cpp"
    )
    target_link_libraries(${runtime_test} PRIVATE GTest::gtest_main)
    target_include_directories(${runtime_test} PRIVATE "${PROJECT_SOURCE_DIR}")
    set_target_properties(
        ${runtime_test}
        PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    )
    if(MSVC)
        target_compile_options(${runtime_test} PRIVATE /W4)
    endif()

    gtest_discover_tests(
        ${runtime_test}
        TEST_PREFIX "${test_family}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 10
    )
endforeach()

# Compile the actual public game headers independently, including their guards twice.
# These are build-time checks, not standalone test applications or mock headers.
foreach(header IN LISTS project_headers)
    get_filename_component(header_name "${header}" NAME_WE)
    set(header_target "header_check_${header_name}")
    add_library(${header_target} OBJECT core/header_tests.cpp)
    target_include_directories(${header_target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(${header_target} PRIVATE "RA_HEADER_TO_TEST=\"${header}\"")
    set_target_properties(
        ${header_target}
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
            UNITY_BUILD OFF
            DISABLE_PRECOMPILE_HEADERS ON
    )

    if(MSVC)
        target_compile_options(${header_target} PRIVATE /W4)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${header_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
