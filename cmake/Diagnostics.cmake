include_guard(GLOBAL)
option(RA_ENABLE_PROFILING "Build native elapsed-time profiling scopes" OFF)

function(raiders_configure_profiling target)
    target_compile_definitions(${target} PUBLIC
        RA_ENABLE_PROFILING=$<BOOL:${RA_ENABLE_PROFILING}>)
endfunction()
