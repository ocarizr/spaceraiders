cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a Windows preset.")
endif()

project(SpaceRaiders LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SpaceRaiders supports Windows only.")
endif()

set(project_name SpaceRaiders)

set(project_sources
    SpaceRaiders.cpp
    renderer.cpp
    PlayField.cpp
    PowerUp.cpp)

set(project_headers
    GameObject.h GameMode.h
    Alien.h AlienLaser.h
    PlayerShip.h PlayerLaser.h PlayerDiagonalLaser.h
    Explosion.h BlockWall.h
    PowerUp.h Input.h
    PlayField.h VSync.h
    renderer.h Vector2D.h
    GameMenus.h)

add_executable(${project_name} ${project_sources} ${project_headers})

set_target_properties(${project_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

if(MSVC)
    target_compile_options(${project_name} PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${project_name} PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
add_subdirectory(engine/diagnostics)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
