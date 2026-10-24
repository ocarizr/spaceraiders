include_guard(GLOBAL)
include(FetchContent)

function(raiders_json_dependency)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nlohmann_json
        URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()

function(raiders_logging_dependencies)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        spdlog
        URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.17.0
        URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(spdlog)
    raiders_json_dependency()
endfunction()
