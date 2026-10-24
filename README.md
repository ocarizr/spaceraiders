[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4d9bf375ab8c4059af697e6c5c65306a)](https://www.codacy.com?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=ocarizr/SpaceRaiders&amp;utm_campaign=Badge_Grade)

# SpaceRaiders

A Windows-only C++ console prototype inspired by Space Invaders. The current game
has lateral movement, aliens, laser shots, walls, power-ups and three game modes.
It is the historical starting point for the planned 2D engine and toolchain;
the rotating ship, asteroids, LuaJIT and editors are future work.

- [Build, run, controls and reference behavior (A01)](docs/legacy-game.md)
- [Macro roadmap and project plans](docs/plano-de-desenvolvimento.md)

## Build on Windows

The supported native toolchain is Windows x64 with Visual Studio 2022's MSVC v143,
the Windows SDK, CMake 3.24 or newer and Ninja. Install Visual Studio's **Desktop
development with C++** workload, including its CMake tools. The A04 build was
verified with MSVC 19.44.35229.0 (toolset 14.44.35207), Windows SDK 10.0.26100.0,
CMake 3.31.6-msvc6 and Ninja 1.12.1. Qt and MinGW are not required.

Open **Developer PowerShell for VS 2022** configured for x64 and run from the
repository root. `cl`, `cmake` and `ninja` must be on PATH. The command-line presets
use that developer environment; they do not install or discover the compiler.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --target SpaceRaiders

cmake --preset windows-release
cmake --build --preset windows-release --target SpaceRaiders
```

Both configurations require C++20. Debug uses CMake's MSVC debug settings
(`/Od`, `/Zi`, debug runtime); Release uses its optimized settings (`/O2`,
`NDEBUG`, release runtime). Compiler warning options belong to the application
target. No global optimization flag overrides Debug, and no forced header include
is needed.

Build outputs and compilation databases stay under the ignored
`out/build/windows-debug` and `out/build/windows-release` directories. In-source
builds are rejected. Keep machine-specific overrides in ignored
`CMakeUserPresets.json`. Visual Studio can open the repository folder and select
the shared [CMake presets](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs?view=msvc-170);
`CMakeSettings.json` has been replaced by those presets. The A01 reference retains
the historical build failure and diagnostic workaround, rather than current build
instructions. Automated tests and CI are planned in A05.

## Run

Launch either build in an interactive Windows console:

```powershell
& ./out/build/windows-debug/SpaceRaiders.exe
# Or: & ./out/build/windows-release/SpaceRaiders.exe
```

Select `1`, `2` or `3`, and use the left/right arrow keys. The default build fires
automatically; Space controls firing only in the `TEST_RUN` compile-time variant.
See the [reference guide](docs/legacy-game.md) for controls and known gameplay issues.
