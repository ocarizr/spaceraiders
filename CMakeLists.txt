cmake_minimum_required(VERSION 3.5)

project(GameTest LANGUAGES CXX)
add_definitions("-Wall -m64 -O3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
set(project_name SpaceRaiders)
set(project_sources
    SpaceRaiders.cpp renderer.cpp
    renderer.h VSync.h Vector2D.h Input.h
    GameMode.h PlayField.h PlayField.cpp
    GameObject.h Explosion.h
    PlayerLaser.h PlayerShip.h
    AlienLaser.h Alien.h
    PowerUp.h PowerUp.cpp)

add_executable(${project_name} ${project_sources})
