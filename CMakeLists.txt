cmake_minimum_required(VERSION 3.5)

project(GameTest LANGUAGES CXX)
add_definitions("-Wall -m64 -O3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}")

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
