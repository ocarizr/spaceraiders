# Space Raiders: macro development roadmap

Updated: 2026-10-24. Source-code baseline inspected: `9d87fe4`.
The current branch contains planning changes; no engine/editor implementation is
claimed by this roadmap. The existing source was read together with both pages of
`IKACppTest.pdf`. No game build or runtime verification was performed while planning.

This is the coordination plan for a reusable 2D engine and its developer ecosystem.
The game is a sample demonstrating the engine, editors, scripting and content
pipeline. Detailed work belongs to independent project plans, each with its own
scope, inputs, outputs, acceptance criteria and backlog. The macro plan owns the
integration contracts and delivery gates; it does not duplicate their tasks.

## Project plans and delivery status

| Project | Authoritative plan | Owned tasks | Delivery state |
| --- | --- | --- | --- |
| C++20 engine, renderer, physics, native UI and preview host | [Engine](plans/engine.md) | A, B, C, D, E, H and J07 | Planned |
| LuaJIT runtime, bindings, VS Code and debugger | [Scripting](plans/scripting.md) | G | Planned |
| Assets/bundles, pipeline, build, CI and Designer SDK | [Asset pipeline](plans/asset-pipeline.md) | F, I, O, P, Q, R and X | Planned |
| Shared C# tooling platform and UI editor | [UI editor](plans/ui-editor.md) | J except J07, and K | Planned |
| C# entity, drawing and animation editor | [Entity editor](plans/entity-editor.md) | L | Planned |
| C# dependency graph and memory inspector | [Bundle inspector](plans/bundle-inspector.md) | S | Planned |
| Cross-language diagnostics | [Observability](plans/observability.md) | T | Planned |
| C# scene/level editor and native playtest | [Level editor](plans/level-editor.md) | W | Planned |
| Authored Space Raiders sample and legacy regression | [Sample game](plans/sample-game.md) | M and N | Planned |

The plans currently define **196 small PR-sized tasks**, including all 131 task IDs
from the previous plan and 65 additions. Task IDs are unique across projects.
Cross-project needs refer to those IDs. The dependency graph must remain acyclic.
Projects may consume each other's incremental interfaces; this does not require
completing a whole project before starting a consumer.

Delivery status is a public product-level summary explicitly requested for this
roadmap: Planned -> Contract ready -> Usable slice -> Integrated -> Verified.
Only update it with a merged PR/reference or reproducible acceptance evidence.
Drafted documentation is not implementation progress. Detailed execution checklists,
agent notes, temporary captures and milestone evidence remain local or CI artifacts,
not additional source-controlled trackers.

## Confirmed constraints

- Engine and native sample/support code use C++20.
- Interactive developer applications use C#: UI/entity/level editors and inspectors.
  A native preview process is an engine service, not a C++ authoring frontend.
- Gameplay and UI behavior use LuaJIT. UI/gameplay source modules remain in distinct
  namespaces in authoring bundles.
- Noninteractive pipeline, validation, code/binary generation and orchestration
  may use C++ or Python. Distribute executable/runtime-complete tooling to designers.
- Repository code, comments, identifiers, diagnostics, tool UI and shipped developer
  documentation use English. Older detailed planning notes may retain Portuguese
  under the approved exception; new task contracts use English.
- Persist authored content in versioned data bundles. Layout/entity/scene definitions
  use XML members; Lua remains editable source in authoring bundles. Shipping bundles
  contain cooked binary descriptors and compatible LuaJIT bytecode, not XML/plain Lua.
- Every executable/module has logging/profiling/memory diagnostic integration:
  engine, game, native preview, pipeline, all C# tools, Python stages and Lua code.
- Editor/playtest content uses the actual native game renderer, not a C# approximation.
- Rendering is 2D with authored drawings, image/glyph sprites, animation and data-driven
  layers. UI chooses its render layer; it is not always forcibly drawn in front.
- Physics supports simple mathematical shapes: square/rectangle, triangle, circle
  and 2D capsule, including rotation. No arbitrary mesh/polygon collision authoring.
  Quadtree broad phase must be checked against brute force and measured.
- A designer receives tool/game binaries, templates and scripting integration;
  authoring, debugging, previewing and cooking new compatible content must not require
  Visual Studio, C++/.NET SDKs, a Python installation or rebuilding native code.
  VS Code plus configured Lua extensions is an explicit scripting-workstation prerequisite.
- No 3D, shader editor, lighting pipeline, terrain system, multiplayer, general Lua IDE
  or second side-scroller sample is included. These do not block the current delivery.

## Key architecture and requirement resolutions

### Native application and render ownership

The engine owns entry point/loop; the game library provides Application composition.
The World owns entities/components and uses generation-checked handles. Native
physics, animation, UI layout and rendering execute in the game or PreviewHost.

C# tools have desktop event loops and documents. They communicate with pipeline and
PreviewHost through versioned CLI/IPC contracts. They may host a native window or
present native-rendered surface pixels. They must not re-rasterize game content.
Authoring overlays are separate. Compare the same viewport, DPI, assets, camera,
layers, color settings and simulation time to verify identical rendering.
Console remains a legacy backend; a native window/offscreen 2D backend serves the
rotated drawings/sprites required by the final sample. Backend choice is a small ADR,
not an implicit commitment to build shader/lighting infrastructure.

### Data-driven content versus generated C++

The previous plan required C++ output for every entity/layout. The newer requirement
that a designer add content without native compilation takes precedence:
normal cooking writes binary descriptors and LuaJIT bytecode to data bundles, and
the existing engine instantiates them using already registered native component/
widget/binding schemas. Asset IDs/catalogs are not frozen into the executable.

Generated C++ remains available for native schema registration/bindings and optional
static embedding in developer builds. Static embedding is not the default designer
path. Adding a new native component/API needs a developer-produced runtime/SDK update;
adding an entity, UI screen, level, animation or Lua behavior using supported types
does not. Native-schema/VM compatibility is distinct from the changing content hash.

Authoring bundles are initially proposed as diffable directories with a manifest;
cooked bundles are indexed binary containers. The pipeline plan closes versions,
IDs, dependency kinds and migrations. Manifests/members are committed; generated
C++, cooked binaries and local backups are not. Runtime logs/traces/snapshots are
operational outputs, not authored content, and stay outside source/package assets.

### LuaJIT, bytecode and IDE experience

JIT compilation occurs during execution; cooking precompiles source to LuaJIT VM
bytecode. Stripping source/debug metadata does not provide C++-style native compilation
or guaranteed secrecy. Native and content compiler builds must use compatible VM
bytecode/frame modes, validated before loading.
[LuaJIT bytecode and JIT controls](https://luajit.org/running.html).

Development preserves source/line maps and supports LuaLS completion, lint/type
diagnostics, formatting and real embedded-VM debugging. Required debugger acceptance:
break/step/locals/watches/errors/coroutines in the actual PreviewHost, not only a
standalone Lua script. Debug mode can disable JIT for reliable stepping; performance
sessions use JIT without debugger hooks. Shipping strips source/debugger capabilities.
The scripting plan owns extension compatibility verification, generated API stubs,
source maps and C# -> VS Code navigation.

### Logs, profiling and footprint

spdlog is the native logging backend candidate/selected baseline, with a project API;
scope timing and trace generation are separate profiling facilities. C# and Python
use idiomatic adapters, Lua uses native bridges and LuaJIT CPU sampling as appropriate.
All share schemas/session IDs; no requirement to force spdlog into every language.
[Native logging capabilities](https://github.com/gabime/spdlog).

Build flags independently control logging, profiling, memory tracking and debugger.
Native/.NET instrumentation-off builds remove collection and costly payload work.
Python uses disabled collection configuration rather than claiming native compile-out.
Queues, capture duration, memory overhead, dropped events and flush behavior are bounded.

Export a tested Chrome Trace Event JSON subset for browser viewing; include process/
thread correlation and clock synchronization metadata. Perfetto accepts legacy JSON
best-effort, so validate our event subset rather than claiming all JSON variants work.
[Trace viewer compatibility](https://perfetto.dev/docs/faq).

Memory views distinguish compressed disk size, decoded estimates, measured live
allocations/capacity/peaks and OS process totals. Track native, .NET, Python, Lua heap,
JIT/cache, allocator overhead and unknown buckets with explicit collector coverage.
A player/UI dependency tree traverses direct/transitive and observed dynamic script
imports. Shared allocations are counted once per actual owner/VM/process, cycles
terminate, and multiple instances remain visible. Snapshot deltas are not automatically
leaks; forced GC, retention/rooting and untracked memory must be disclosed in results.
The inspector extends the existing graph tool instead of adding another full application.

### Main sample

A freely moving and rotating ship in a finite zero-gravity arena uses thrust,
inertia and reverse thrust to brake. It wraps top-to-bottom and left-to-right while
preserving motion/orientation. Aliens, fragmenting asteroids and spawn animations
are authored with the tools and Lua. Wrapping contact pairs, interpolation and CCD
are explicit engine work; this is not an infinite world.
A side-scroller is a possible engine reuse case, not a second promised sample.

## Shared contracts and handoffs

| Contract | Producer | Consumers | Required evidence |
| --- | --- | --- | --- |
| Native component/widget schema and handles | Engine + sample registration | Lua, pipeline, inspectors/editors | Versioned types/defaults/constraints; unknown type rejection |
| Native frame surface and animation clock | Engine/PreviewHost | All editor previews and playtest | D10 frame parity; no C# scene rasterizer |
| Source/cooked bundles and catalog compatibility | Asset pipeline | Runtime, scripting, all tools | Round-trip, corruption rejection, new content ID in existing runtime |
| Tooling CLI/IPC and SDK manifest | Pipeline + shared C# tooling | Designer tools, preview, CI | Version mismatch/cancel/failure and last-good-state tests |
| Lua API definitions/debug/source maps | Scripting | C# tools, VS Code, designer SDK | Embedded breakpoint/step/watch and Shipping exclusions |
| Log/trace/snapshot schemas and allocation semantics | Observability | Every executable; bundle inspector | ON/OFF, queue saturation, measured/estimated/shared accounting |
| Scene instances and playtest control | Level editor + runtime | Designers and sample | Play/pause/step/reset without packaging or document mutation |

When changing a shared contract, its producer owns the change; list all consumers
and update only those affected. Keep compatibility or introduce a versioned migration.
No duplicate XML parser, physics solver, renderer or Lua VM implementation in C#.

## Integration gates and coordination order

These are demonstrations across projects, **not large PRs**. Projects work on ready
local tasks and expose small contracts incrementally. The task dependencies are the
authority when an approximate phase list is insufficient.

| Gate | Required handoffs | Exit evidence |
| --- | --- | --- |
| GATE-0: reproducible base | A01, Q01, A04/A05; T01-T04 and native extraction starts | Clean supported build/test; native logging and one trace scope |
| GATE-1: native vertical slice | B07, C03, D05, E05, G01, R02/R05 | App-independent engine, rotated simple shapes and source bundle in native renderer |
| GATE-2: exact tooling preview | J06-J08, D10, K05, L07 | UI/entity/animation preview equals native game output |
| GATE-3: usable Lua environment | G11-G15 | Completion/lint and breakpoint/step/watch in embedded preview |
| GATE-4: designer content loop | X02-X06, W03/W04, G16, P07 | Binaries-only authoring, playtest and cooked bytecode content update |
| GATE-5: performance visibility | T12-T15, G17, S07-S11, W05 | Correlated traces plus player/UI memory graphs with shared/unknown costs |
| GATE-6: sample and release | N07, E08/E09, O07/O08, W06, X07, P06 | Ship thrust/rotation/wrap, aliens/asteroids, selective CI and clean-machine SDK acceptance |

Observability contracts begin early rather than waiting for the game to become slow.
A project does not need its full feature set to satisfy an initial handoff.
All gate delivery states are currently Planned; none is marked complete by this edit.

## Build and CI approach

Evaluate CMake + .NET/MSBuild against Bazel in Q01 with a small representative
C++/C#/generated-content slice. Bazel was suggested, not mandated. Start from CMake
native targets and SDK-style C# projects/solution filters unless the ADR justifies
a migration; avoid maintaining two complete build graphs. Build/test individual
projects and their dependencies without building unrelated tools.
[Native target selection](https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-a-project),
[.NET project builds](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-build).

Use a small number of workflows: CI with impact-based jobs and a stable mandatory
gate; release/manual product selection for game, tools/Designer SDK, or both.
A tools-only build compiles needed native support/PreviewHost, not the unrelated
sample executable. A content cook compiles neither native nor managed source.

Classify changed paths plus transitive contract consumers: diagnostics/surface/schema
changes can affect several projects; Lua-only, content-only, Python-only or one
C# frontend change normally skips unrelated native/tool builds. Always start the
workflow containing the required gate; legitimately skipped jobs are distinct from
failed/cancelled required jobs. Unknown impact falls back to full verification.
The pipeline plan owns the detailed CI matrix and package contents.

## PR scope, validation and local files

Each task is a PR-sized unit with inputs, file budget and acceptance. Aim for 3-8
changed paths, at most about 10 including tests, build registration, schemas and
bundle manifests. Reserve up to two integration paths only when the total still
fits. Split tasks with suffixes before exceeding the budget. Moving a large set
of files is not an excuse to hide an oversized diff. Keep every PR buildable.

A new prompt on a working branch continues the work. On master, verify clean/safe
synchronization with origin and create a branch before editing. At each activity's
end, review and stage all submittable files, provide an English commit message,
and do not commit/push without explicit instruction. AGENTS.md, agent-only helpers,
scratch verification scripts and temporary evidence remain local-only.

Meaningful checks depend on the change: domain tests, shared-contract tests,
renderer parity, embedded debugger tests, bundle corruption/version tests, build
incrementality and clean-machine SDK acceptance. Document manual UI/IDE acceptance
where automation cannot establish the behavior. No claim of full memory attribution,
bit-for-bit platform-independent physics, bytecode secrecy or performance improvement
without supporting evidence.

## Current baseline and next action

The real checkout is a Windows console aliens game with lateral player movement,
walls, power-ups and three modes. It does not yet implement the target rotating/
moving ship, components, Lua, XML/bundles or editors. No application source includes
a .cpp in the inspected revision; transitively included headers, ownership/timing,
collisions and generated CMake/IDE files are concrete issues. The engine plan retains
the detailed source/PDF diagnosis and legacy stabilization tasks.

Next implementation slice: A01 and Q01, then A04/A05 and early diagnostics contracts,
while extracting only the minimal engine interfaces needed for the first vertical
slice. This roadmap does not authorize a broad rewrite or a native build-system
migration merely because the planning package is now larger.
