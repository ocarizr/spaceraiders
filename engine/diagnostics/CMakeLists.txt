option(RA_ENABLE_LOGGING "Build native structured logging" ON)
set(RA_LOG_ACTIVE_LEVEL 2 CACHE STRING "Compiled minimum log level: 0 trace .. 5 critical, 6 off")
if(NOT RA_LOG_ACTIVE_LEVEL MATCHES "^[0-6]$")
    message(FATAL_ERROR "RA_LOG_ACTIVE_LEVEL must be an integer from 0 to 6")
endif()

include(${PROJECT_SOURCE_DIR}/cmake/Diagnostics.cmake)
add_library(raiders_diagnostics STATIC src/Log.cpp src/Profile.cpp src/TraceWriter.cpp)
raiders_configure_profiling(raiders_diagnostics)
target_include_directories(raiders_diagnostics PUBLIC include)
target_compile_features(raiders_diagnostics PUBLIC cxx_std_20)
set_target_properties(raiders_diagnostics PROPERTIES CXX_EXTENSIONS NO)
target_compile_definitions(
    raiders_diagnostics
    PUBLIC RA_ENABLE_LOGGING=$<BOOL:${RA_ENABLE_LOGGING}> RA_LOG_ACTIVE_LEVEL=${RA_LOG_ACTIVE_LEVEL}
)
if(MSVC)
    target_compile_options(raiders_diagnostics PRIVATE /W4 /utf-8)
endif()

if(RA_ENABLE_LOGGING)
    include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)
    raiders_logging_dependencies()
    target_link_libraries(raiders_diagnostics PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json)
endif()

if(RA_ENABLE_PROFILING)
    include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)
    raiders_json_dependency()
    target_link_libraries(raiders_diagnostics PRIVATE nlohmann_json::nlohmann_json)
endif()

if(BUILD_TESTING)
    include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)
    raiders_test_dependency()
    include(GoogleTest)
    add_executable(trace_tests tests/trace_tests.cpp)
    # Custom GoogleTest entry point dispatches the crash-recovery subprocess.
    target_link_libraries(trace_tests PRIVATE raiders_diagnostics GTest::gtest)
    if(RA_ENABLE_PROFILING)
        target_link_libraries(trace_tests PRIVATE nlohmann_json::nlohmann_json)
    endif()

    gtest_discover_tests(
        trace_tests
        TEST_PREFIX "diagnostics.trace."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 30
    )

    add_executable(profile_toggle_tests tests/profile_toggle_tests.cpp)
    target_link_libraries(profile_toggle_tests PRIVATE raiders_diagnostics GTest::gtest_main)
    gtest_discover_tests(
        profile_toggle_tests
        TEST_PREFIX "diagnostics.profile."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 15
    )

    # Exercise the consumer's disabled macro even in a profiling-enabled build.
    add_executable(profile_disabled_macro_tests tests/profile_toggle_tests.cpp)
    target_compile_definitions(profile_disabled_macro_tests PRIVATE RA_TEST_PROFILE_DISABLED=1)
    target_link_libraries(
        profile_disabled_macro_tests
        PRIVATE raiders_diagnostics GTest::gtest_main
    )
    gtest_discover_tests(
        profile_disabled_macro_tests
        TEST_PREFIX "diagnostics.profile_disabled."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 15
    )

    if(RA_ENABLE_LOGGING)
        add_executable(log_tests tests/log_tests.cpp)
        target_link_libraries(
            log_tests
            PRIVATE raiders_diagnostics nlohmann_json::nlohmann_json GTest::gtest_main
        )
        gtest_discover_tests(
            log_tests
            TEST_PREFIX "diagnostics.log."
            DISCOVERY_MODE PRE_TEST
            PROPERTIES TIMEOUT 30
        )
    endif()

    foreach(mode IN ITEMS filtered disabled)
        add_executable(log_${mode}_tests tests/log_filter_tests.cpp)
        target_link_libraries(log_${mode}_tests PRIVATE raiders_diagnostics GTest::gtest_main)
        if(mode STREQUAL "disabled")
            target_compile_definitions(log_${mode}_tests PRIVATE RA_TEST_DISABLED=1)
        endif()

        gtest_discover_tests(
            log_${mode}_tests
            TEST_PREFIX "diagnostics.log_${mode}."
            DISCOVERY_MODE PRE_TEST
            PROPERTIES TIMEOUT 15
        )
    endforeach()
endif()
