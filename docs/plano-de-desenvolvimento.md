# Space Raiders: macro development roadmap

Updated: 2026-10-25. Merged progress checked against master `43ade50` (#16).
The original source/PDF inspection used `9d87fe4`; its legacy findings remain
historical context. A01-A05, Q01-Q02 and T01-T05 are merged. T06's Python telemetry
adapter is implemented and locally validated, pending review/merge; job-system
implementation remains planned.

This is the coordination plan for a reusable 2D engine and its developer ecosystem.
The game is a sample demonstrating the engine, editors, scripting and content
pipeline. Detailed work belongs to independent project plans, each with its own
scope, inputs, outputs, acceptance criteria and backlog. The macro plan owns the
integration contracts and delivery gates; it does not duplicate their tasks.

## Project plans and delivery status

| Project | Authoritative plan | Owned tasks | Delivery state |
| --- | --- | --- | --- |
| C++20 engine, renderer, physics, native UI and preview host | [Engine](plans/engine.md) | A, B, C, D, E, H and J07 | A01-A05 baseline merged; extraction planned |
| LuaJIT runtime, bindings, VS Code and debugger | [Scripting](plans/scripting.md) | G | Planned |
| Assets/bundles, pipeline, build, CI and Designer SDK | [Asset pipeline](plans/asset-pipeline.md) | F, I, O, P, Q, R and X | Contract ready (Q01/Q02); remaining work planned |
| Shared C# tooling platform and UI editor | [UI editor](plans/ui-editor.md) | J except J07, and K | Planned |
| C# entity, drawing and animation editor | [Entity editor](plans/entity-editor.md) | L | Planned |
| C# dependency graph and memory inspector | [Bundle inspector](plans/bundle-inspector.md) | S | Planned |
| Cross-language diagnostics | [Observability](plans/observability.md) | T | Native/managed merged (T01-T05); Python T06 locally validated, pending merge |
| Shared native job system and C#/Python packages | [Jobs](plans/jobs.md) | V | Planned |
| C#/.NET 10 graphical environment setup and repair | [Environment setup](plans/environment-setup.md) | Y | Planned |
| C# scene/level editor and native playtest | [Level editor](plans/level-editor.md) | W | Planned |
| Authored Space Raiders sample and legacy regression | [Sample game](plans/sample-game.md) | M and N | Planned |

The plans currently define **226 small PR-sized tasks**: the previous 207 plus
19 job-system tasks (V01-V19). Task IDs are unique across projects.
Cross-project needs refer to those IDs. The dependency graph must remain acyclic.
Projects may consume each other's incremental interfaces; this does not require
completing a whole project before starting a consumer.

Delivery status is a public product-level summary explicitly requested for this
roadmap: Planned -> Contract ready -> Usable slice -> Integrated -> Verified.
Only update it with a merged PR/reference or reproducible acceptance evidence.
Drafted documentation is not implementation progress. Detailed execution checklists,
agent notes, temporary captures and milestone evidence remain local or CI artifacts.
The explicitly requested [task dependency diagram](task-dependencies.md) is the
shared navigation/status view: white means waiting on prerequisites, green means
ready, and red means completed. It covers every task and records merged completion
references. Update its completed nodes and recompute readiness at the end of each
completed task; do not treat a green prerequisite as completed. Detailed task
definitions and dependency edits stay in their owning project plans.

## Confirmed constraints

- Project-owned layout is defined in [the build contract](build-system.md#repository-layout-and-project-ownership):
  `engine/`, `game/`, `tools/dotnet/`, `tools/pipeline/` and `tools/native/` are
  separate areas. Sources, tests and fixtures belong to their project; only
  cross-project integration checks belong in root `tests/integration/`.
  This overrides older flat path examples in the individual backlogs. Relocate
  existing files incrementally in small, buildable tasks; Q02 establishes policies
  without combining those moves with the environment configuration delivery.

- Support and validate Windows only. Linux support, Linux CI and portability work
  are outside this project.
- Engine and native sample/support code use C++20.
- A shared C++20 job library supplies native engine/tools and C#/Python wrappers.
  Game/main and render threads orchestrate a configurable CPU worker pool, initially
  targeting four or five workers where appropriate. Dependency counters, nested
  child completion, continuations and explicit affinity replace intermediate
  main-thread waits. Wrappers preserve one scheduler implementation; Python GIL
  limits and process/thread budgets are explicit. See the [job contract](plans/jobs.md).
- Interactive developer applications use C# with .NET 10: UI/entity/level editors, inspectors
  and the graphical environment setup manager.
  A native preview process is an engine service, not a C++ authoring frontend.
- Reusable managed libraries may be NuGet package producers/consumers; Python
  modules are built as wheels and installed into reproducible build/runtime venvs.
  Setup detects Ready/Missing/Needs repair states and prepares selected profiles.
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
  authoring, debugging, previewing and content-only packaging must not require
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

### Two execution paths: editing and packaged runtime

| Mode | Content input | Load path | Cooking/native content build |
| --- | --- | --- | --- |
| Editing and playtest | Serialized XML/Lua members of source bundles | Development-only source provider deserializes to live runtime objects/typed descriptors | None: no cooked bundle, bytecode artifact or generated C++ prerequisite |
| Packaged game | Optimized binary content and packaged LuaJIT bytecode | Shipping binary/compiled provider; same runtime systems and behavior | Packaging validates/cooks serializable data, generates eligible C++ data/bindings and compiles/links them in full-game packaging |

Both are required. Deserializing XML and allocating runtime objects in memory is
normal loading, not an offline cooking pass. Editing may cost more CPU/memory and
that is acceptable. Save, refresh, reload and Play must work before any packaging,
even with no cooker outputs or native toolchain present. The same renderer,
World, physics, UI, animations and gameplay operate after either provider loads data.

Cooking belongs to explicit packaging. Convert eligible XML/entity/UI/scene/
animation data into efficient binary representations, with versioned schemas,
indexes/offsets and appropriate memory layout. Full-game packaging can generate C++
for serializable descriptors/registrations and compile it into the game's native
binary; do not remove that route or restrict C++ generation to schemas alone.
Lua bytecode is produced for packaging, separate from live source debugging.

A separate content-only packaging profile can emit compatible binary data bundles
for an existing runtime without linking it again. That does not replace full-game
packaging or imply that the editor should use cooked data. Native toolchains belong
to developer/CI full-game packaging; designers edit/test using distributed binaries.
Adding native component types still requires a developer-produced runtime update.

The core instantiation/render/simulation systems do not depend on XML. A development
source-provider adapter reuses authoring deserializers and is linked/shipped only
with editing/development hosts; Shipping omits it. A binary/compiled provider serves
packaged content. Equivalence tests load the same authored scene through both
paths, including generated-C++ content in full-game packaging, and compare behavior
and native frames. Profiling/memory reports identify the active mode; costs need
not be identical between deserialized editing and optimized packaged runtime.

Authoring bundles are diffable directories with manifests; cooked bundles are indexed
binary containers. Manifests/members are committed; generated C++, cooked binaries,
local backups and diagnostic captures are not source-controlled content.

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
| Source/packaged providers and catalog compatibility | Asset pipeline | Runtime, scripting, all tools | Source-only Play; round-trip, corruption rejection, source/binary/generated-C++ parity |
| Tooling CLI/IPC and SDK manifest | Pipeline + shared C# tooling | Designer tools, preview, CI | Version mismatch/cancel/failure and last-good-state tests |
| Lua API definitions/debug/source maps | Scripting | C# tools, VS Code, designer SDK | Embedded breakpoint/step/watch and Shipping exclusions |
| Log/trace/snapshot schemas and allocation semantics | Observability | Every executable; bundle inspector | ON/OFF, queue saturation, measured/estimated/shared accounting |
| Scene instances and playtest control | Level editor + runtime | Designers and sample | Play/pause/step/reset without packaging or document mutation |
| Job handles, dependency/child completion, lanes and native ABI | Shared jobs | Runtime/render/physics, C# tooling, Python pipeline | Same DAG semantics; bounded queues; cancellation/shutdown; trace correlation; GIL/GC and serial-oracle tests |

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
| GATE-1: native editing slice | B07, C03, D05, E05, G01, R02/R09 | Source XML/Lua deserialized in native renderer before cooking exists |
| GATE-2: exact tooling preview | J06-J08, D10, K05, L07; V15/V17 | UI/entity/animation preview equals native game output with explicit frame/job ownership |
| GATE-3: usable Lua environment | G11-G15 | Completion/lint and breakpoint/step/watch in embedded preview |
| GATE-4: designer content loop and packaging | X02-X06, W03/W04, G16, P07, I08 | Source-only editing/Play; explicit packaging to binary/bytecode, including full-game generated-C++ route |
| GATE-5: performance visibility | T12-T15, G17, S07-S11, W05; V08 | Correlated traces including job dependencies, plus player/UI memory graphs with shared/unknown costs |
| GATE-6: sample and release | N07, E08/E09, O07/O08, W06, X07, P06; V19 | Ship sample, selective CI, clean-machine SDK and measured runtime/tool/pipeline scheduling acceptance |

Observability contracts begin early rather than waiting for the game to become slow.
A project does not need its full feature set to satisfy an initial handoff.
All gate delivery states are currently Planned; none is marked complete by this edit.

## Build and CI approach

Q01 recommends CMake/Ninja for C++20, SDK-style .NET 10/MSBuild/NuGet for C#,
and installed Python wheel modules in isolated environments. The
[ADR and comparative spike](decisions/0001-monorepo-build.md) document the decision:
both native systems worked, but managed IDE/desktop/package ownership favors the
standard .NET path here. Bazel remains a future reconsideration option, not a
second maintained build graph.

The [build/environment contract](build-system.md) defines package handoffs,
project-level builds and graphical/headless setup responsibilities. Q02 established
version/environment policies; Q03-Q07 implement the remaining project/package
foundations and environments. Y01-Y07 own the independent self-contained C# setup
product. Each is a separate task.

Use a small number of workflows: CI with impact-based jobs and a stable mandatory
gate; release/manual product selection for game, tools/Designer SDK, or both.
A tools-only build compiles needed native support/PreviewHost, not the unrelated
sample executable. Editing never triggers cooking. Content-only packaging needs
no native compile; full-game packaging compiles generated eligible C++ content.

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

Merged foundations include the legacy reference, source/IDE hygiene, C++20 presets,
native tests/CI, build decision/environment policies and native/managed telemetry.
The [task diagram](task-dependencies.md) records twelve merged task IDs and their
PRs, plus the locally validated T06 delivery awaiting review/merge. The pytest
maintenance PR (#14) is not another task.
Q05/Q06 remain separate package-foundation tasks despite earlier reusable slices.

The [diagram's green frontier](task-dependencies.md#available-now) lists the current
ready choices. Completing T06 unlocks no new task until its consumers' other
prerequisites are satisfied. V01 is an
independent design-contract option, followed by future V implementation tasks.
Choose one ready task with the maintainer; never execute adjacent work automatically.
Use branches named `<TASK_ID>_<short_description>`. Completion must update the task
diagram and newly unlocked green nodes as part of the same reviewed delivery.
