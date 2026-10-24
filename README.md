[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4d9bf375ab8c4059af697e6c5c65306a)](https://www.codacy.com?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=ocarizr/SpaceRaiders&amp;utm_campaign=Badge_Grade)

# SpaceRaiders

A Windows-only C++ console prototype inspired by Space Invaders. The current game
has lateral movement, aliens, laser shots, walls, power-ups and three game modes.
It is the historical starting point for the planned 2D engine and toolchain;
the rotating ship, asteroids, LuaJIT and editors are future work.

- [Build, run, controls and reference behavior (A01)](docs/legacy-game.md)
- [Macro roadmap and project plans](docs/plano-de-desenvolvimento.md)

The current sources use C++17. The verified Windows/MSVC diagnostic build needs a
temporary forced include of `<chrono>`; the reference guide records both the
original build failure and the workaround. C++20 adoption and build-system changes
are separate tasks. Run the executable in an interactive Windows console, select
`1`, `2` or `3`, and use the left/right arrow keys. The default build fires
automatically; Space controls firing only in the `TEST_RUN` compile-time variant.
