# Legacy game reference (A01)

Reference date: 2026-10-24. Repository baseline: `b9a2d87`.

This documents the existing Windows Space Invaders-style prototype before its
replacement by the planned engine/toolchain and a different sample game. It is a
behavioral reference for review and incremental changes, not a requirement to retain
every old mode or defect in the final product. Only Windows is supported and
validated. This task makes no gameplay, compiler-standard or build-system changes.
The CMake/.NET versus Bazel evaluation belongs to Q01 after A01 review.

## What exists today

`SpaceRaiders.cpp` owns `main`, startup, menus, the world, random seed and the loop.
`PlayField` owns the active object list, deferred add/remove queues, input, modes
and wave construction. Objects use inheritance and type-name strings, not a
component system. `Renderer` writes one-character sprites to a Windows console.
There are no external gameplay asset files, save games or Lua scripts to load.

The arena is 80 columns by 28 rows. One player starts at `(40, 27)`; aliens start
at random X positions from 1 to 78 and Y positions from 0 to 10. Walls are spawned
at random X positions from 5 to 75 and Y positions from 5 to 15. Placements are
independent: overlap prevention is not implemented.

Movement is lateral and velocity is applied directly. There is no player rotation,
thrust, inertia, screen wrapping, asteroid fragmentation, authored animation,
XML/bundle loading or editor. Those describe the future sample, not this baseline.

## Windows build reproduction

### Tools and environment verified

The local reference check used Windows x64, Visual Studio 2022 Community's x64
developer environment, MSVC `19.44.35229.0` (toolset directory `14.44.35207`),
CMake `3.31.6-msvc6`, Ninja `1.12.1` and the Windows SDK. The project currently
requests C++17 and requires no Qt runtime. Old Qt/MinGW paths in committed CMake
cache/IDE files are historical artifacts, not portable build instructions.

Use **Developer PowerShell for VS 2022** configured for x64, with Desktop development
with C++ and C++ CMake tools installed. Run these commands from the repository root.
They use a fresh build directory outside the checkout so generated files do not
become proposed source changes. Use separate directories for the original check
and the diagnostic workaround.

### Original configuration: failure reproduced

```powershell
cmake --version
ninja --version
$originalBuild = Join-Path $env:TEMP 'SpaceRaiders-A01-original'
cmake -S . -B $originalBuild -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build $originalBuild --target SpaceRaiders --parallel 4
```

Configuration succeeded. Compilation failed in `SpaceRaiders.cpp` at the two
`std::chrono::high_resolution_clock::now()` calls: MSVC reported C2039/C3083/C3861.
That source uses `<chrono>` facilities but does not include `<chrono>` explicitly;
this installed standard library does not supply them through `<thread>` alone.
Do not describe this as a successful clean build of the original configuration.

The existing `-m64` and `-O3` options also produced D9002 warnings and were ignored
by MSVC. `-Wall` enables extensive compiler/SDK warnings. CMake emitted a warning
about the project's old minimum-version compatibility. A01 records these issues;
fixing build configuration/includes belongs to the later implementation tasks.

### Diagnostic workaround: build succeeded without editing sources

```powershell
$baselineBuild = Join-Path $env:TEMP 'SpaceRaiders-A01-chrono'
cmake -S . -B $baselineBuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=/FIchrono
cmake --build $baselineBuild --target SpaceRaiders --parallel 4
& (Join-Path $baselineBuild 'SpaceRaiders.exe')
```

`/FIchrono` asks MSVC to force-include the standard header in this build only.
Configuration, all four application source compilations and linking succeeded;
the ignored-option and other legacy warnings remain. This is a temporary,
MSVC-specific compatibility workaround for observing the current behavior, not the
project's future build setup or a source-code fix. No compiler/cache workaround
has been committed to `CMakeLists.txt` or `CMakeSettings.json`.

The paths above are reproducible choices, not required names. The A01 check used
equivalent external build directories rather than modifying the checked-in CMake
artifacts. Existing build directories remember their generator/compiler/flags;
use a fresh path when changing these inputs.

## Start, control and exit a session

Use an interactive Windows console with room for the 80x28 arena and an additional
HUD row; a viewport around 100x35 avoids clipping during review. The renderer emits
single-byte glyph values and uses `SetConsoleCursorPosition`, so character
appearance depends on the console font/encoding. UTF-8 log capture may replace
the background/laser glyphs; that alone is not evidence that game objects changed.

1. Launch `SpaceRaiders.exe`. The menu shows `SpaceRaiders` and three numbered modes.
2. With the console focused, press the **top-row** `1`, `2` or `3`. The code polls
   Win32 virtual keys `0x31`-`0x33`; numpad selection is not implemented.
3. Move the player with Left/Right. In the default build, firing is automatic and
   randomized. There is no mouse input, pause key, rotation or thrust control.
4. When a mode finishes, the loop stops and a game-over screen displays the score.
   `main` then calls `getchar()`: press Enter if it is still waiting. Buffered
   console input can affect this final wait. There is no return-to-menu command;
   relaunch the executable to choose another mode.
5. To interrupt a running game or menu, use Ctrl+C or close its console. This is an
   external interruption, not a game-provided quit action or a normal game-over exit.

| Input | Current behavior | Source |
| --- | --- | --- |
| Top-row 1/2/3 | Select a mode at startup | `GameMenus::SelectOptionFromStartMenu` |
| Left / Right | Decrease/increase player X; both together leave X unchanged | `PlayerShip::Update` |
| Space, default build | Does not control shots; `KeyboardWithRandomFire::Fire` samples RNG | `Input.h`, `SpaceRaiders.cpp` |
| Space, `TEST_RUN` build | Requests a shot subject to delay/available slots | `KeyboardInput::Fire`, `PlayerShip::Update` |
| Enter after game over | Supplies input to the final `getchar()` wait | `SpaceRaiders.cpp` |

`TEST_RUN` is a compile-time preprocessor definition, not a CLI switch or menu mode.
The line defining it in `SpaceRaiders.cpp` is commented out. To inspect that variant
without editing a source file, use a separate MSVC build directory and pass
`'-DCMAKE_CXX_FLAGS=/FIchrono /DTEST_RUN'` when configuring CMake. This variant's
interactive keyboard/Space behavior was read from source, not playtested in A01.
`RndInput` also exists, but the current `main` never selects it.

## Mode behavior

| Menu option | Initial population | End condition implemented by the mode | Wave behavior |
| --- | --- | --- | --- |
| 1 - Until Player Death | One player; 15-30 aliens; 5-15 walls | Player is absent | New random alien/wall wave when no active aliens remain |
| 2 - Time Attack (59 seconds) | Same construction as option 1 | Timer reaches zero **or** player is absent | Same wave creation check as option 1 |
| 3 - Destroy All Aliens | One player; exactly 50 aliens; 5-15 walls | No active aliens **or** player is absent | The outer loop still queues a wave when aliens disappear, even if the mode has just ended |

The timer starts at 59,000 ms and subtracts `VSync::GetFramePeriod()` on each mode
update. This is simulated time based on the current pacing implementation, not a
verified 59-second wall-clock guarantee. A player's death can end Time Attack early.
The RNG is a global `std::default_random_engine`, seeded with `1` in `main`.

Each iteration updates existing objects and the mode, processes object queues,
checks whether another wave is needed, builds render items, presents the console
and asks `VSync` for a sleep period. Newly queued objects do not update until a
later iteration. A mode can see an object before its queued removal is processed.

At the end, `main` deletes active objects before constructing the game-over menu.
Consequently the last gameplay scene is cleared. It then passes `world.HasPlayer()`
to the result menu **after clearing**, so the menu chooses `You Died!` even when
the player survived the mode. These are baseline defects, not target behavior.

## Objects, shots and score

| Object | Representation | Current behavior from source |
| --- | --- | --- |
| Player | `P` | Lateral movement, initial speed 4; initial fire delay 0.4; no X clamp/wrap |
| Alien | `A` | Moves horizontally at speed 2; reverses and descends one row at horizontal bounds |
| Better alien | `B` | Two hit points and twice the original movement speed; worth 20 instead of 10 |
| Player shot | byte `0xBA` | Moves upward at speed 4; tests aliens and walls by integer-cell equality |
| Diagonal shots | backslash / slash | Move upward and sideways at speed 4 on each axis |
| Alien shot | vertical bar | Moves downward at speed 4; tests player/walls |
| Wall | underscore | Five hit points from player fire; alien overlap pushes it down through recursive updates |
| Explosion | asterisk | Requests removal after 0.25 units of simulated seconds |
| Power-up | `U` | Falls at speed 3 and applies an enhancement on player contact |

Normal/better alien kills add 10/20 score. Aliens gain random energy each update;
when energy reaches 2 they immediately transform. Their fire probability is tested
against 0.5 per update while alien slots remain; better aliens use the same test.
The wave constructor sets available alien laser slots to the wave's alien count.
The initial player laser-slot count is five.

Alien deaths have approximately a 10% drop chance. A second sample in `[0,10]`
selects SpeedBoost below 0.3, FireBoost below 0.6, otherwise TripleFire: conditional
weights are about 3%, 3% and 94%, not equal thirds. Speed and fire-delay boosts use
factors 1.5 and 0.5; triple fire adds two diagonal shots. The current code does not
expire these upgrades. The diagonal type name is not included in the normal
player-laser slot accounting.

Collision is integer-coordinate equality, not continuous collision detection or a
shape-based physics solver. Player shots can hit aliens/walls; alien shots can hit
player/walls. Laser-versus-laser collisions are absent. A shot marked for removal
can still participate in iteration before queues are processed. Do not treat these
limitations as intended contracts for the future physics implementation.

## Original exercise versus current implementation

The [original two-page exercise](../IKACppTest.pdf) is a historical specification.
The table distinguishes its requirements from source findings; these findings
were not all individually reproduced by interactive play.

| Exercise requirement | Current implementation / difference | Follow-up plan |
| --- | --- | --- |
| Seeded deterministic run with fixed duration | Seed is fixed; time, input polling and standard-library RNG details prevent a general determinism claim | A08, A11 |
| Freeze the scene and display score after death | Active objects are cleared before the final menu; surviving player is also reported dead | A09 |
| Any alien reaching the bottom kills the player | The bottom check additionally requires matching the player's integer position | A09 |
| Alien/player collision destroys both | That branch queues player removal but does not consistently remove the alien | A09 |
| Better alien has higher fire rate | It gains health/speed; the firing test remains the same | A11 |
| Transformation timing remains random after sufficient energy | Transformation occurs immediately at the energy threshold | A11 |
| Lasers destroy colliding ships/other lasers with an explosion | Only selected ship/wall pairs are checked; no laser-versus-laser resolution | A10 |
| Temporary speed/fire boosts | Buffs have no duration/expiry | A11 |
| Continuous waves and destructible blocking walls | Both exist; generation also runs after a mode's end decision, and wall movement is recursive | A09, A10 |
| Keyboard control and three-direction fire | Input variant and triple fire exist; diagonal slots are not accounted for like normal player lasers | A10 |

Build hygiene, ownership, timing and rule fixes remain separate planned work.
This document does not implement them or expand A01 into those tasks.

## Verification performed and its limits

| Check | Result | Evidence boundary |
| --- | --- | --- |
| Unmodified CMake/MSVC configuration | Configured; compilation failed on missing chrono declarations | Actual Windows compiler run |
| `/FIchrono` diagnostic build | All four application translation units compiled; executable linked | Same repository sources; extra build flag only |
| Diagnostic executable startup | Console menu and its three labels appeared; process was interrupted from the menu | Actual executable in a pseudo-console; no interactive mode selection confirmed |
| Three mode lifecycles | Each mode reached game-over under the local no-movement scenario below | Existing classes driven by a local characterization harness, not the executable's input/renderer loop |
| Isolated Time Attack expiry | 296 mode updates, 59,016 simulated ms; player still present before clearing | No gameplay updates/collisions; first period 16 ms, subsequent periods 200 ms |
| Final-menu result after surviving timer | `You Died!` after clearing objects | Existing `GenerateGameOverMenu` called in the same order as `main` |
| Repeated seeded characterization | Identical output in repeated runs of the same harness/toolchain | Does not establish cross-toolchain or interactive determinism |
| Interactive controls, glyph appearance, all three full playthroughs | Not verified manually | Review steps below remain necessary |

The temporary harness used `PlayField.cpp`, `PowerUp.cpp` and the existing object/
mode classes without modifying them. It recreated population/update/queue/wave
order with seed 1, no movement, the same random-fire sampling and a zero-cost
frame passed to `GetSyncPeriod`. It omitted rendering, wall-clock sleeps and Win32
keyboard polling. The main executable was built/run separately as described above.
Helpers, logs and binaries stayed outside the repository and are not deliverables.

| Harness scenario, MSVC above | Initial aliens | Update ticks until mode ended | Final score | Player present before final clear |
| --- | --- | --- | --- | --- |
| Until Player Death | 21 | 124 | 40 | No |
| Time Attack, normal gameplay | 21 | 124 | 40 | No |
| Destroy All Aliens | 50 | 47 | 20 | No |

These are narrow characterization results, not portable golden scores or a promise
about an interactive run. The first two scenarios ended on death, not timer expiry;
the separate timer-only check covered expiry. No winning Destroy All Aliens
playthrough, leak analysis, performance benchmark or complete collision matrix was
verified in A01. Fast-frame `VSync` selects a 200 ms period; its threshold ordering
and possibly negative sleep periods are source findings for A08.

## Manual review procedure

Using the diagnostic executable in an actual interactive Windows console:

1. Launch it separately for each mode, focus the console, select top-row 1/2/3 and
   confirm that the mode starts. Observe the HUD and alien/wall populations.
2. Hold Left, Right and then both. Confirm lateral movement and automatic shots;
   test Space control separately only if the `TEST_RUN` variant was built.
3. In each mode, observe an end condition, final score/message and Enter-to-close
   behavior. For Time Attack, distinguish death from surviving until timer expiry;
   for Destroy All Aliens, distinguish death from destroying the wave.
4. Review aliens changing appearance, walls taking hits, falling power-ups and
   triple fire when those random events occur. Record observed defects separately
   from the intended replacement game's behavior.

This is reusable developer documentation, not a completed test checklist. A01's
first pass is ready for review with the build issue and manual-validation limits
explicit; neither Q01 nor a gameplay fix has been started.
