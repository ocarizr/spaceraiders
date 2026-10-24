# Cross-language logging, profiling and memory diagnostics

Status: Planned. Implementation has not started.

## Scope and independent execution

A shared project covering all C++20, C#, Python and Lua execution paths.
Exports: structured log/trace/snapshot schemas, process/session correlation,
compile/build switches, bounded collectors and memory attribution contracts.
Consumers: every runtime, pipeline CLI, editor, preview host and game.
First slice: one C++ scope and one C# command produce valid traces/logs with shared
session context; instrumentation-off builds do not evaluate trace arguments.
The inspector consumes this project's snapshots rather than inventing RAM estimates.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Implementation backlog




## Design constraints

C++ logging uses a pinned spdlog backend behind a project API. spdlog supports
async logging and compile-time log filtering; it does not itself define a complete
CPU profiler. Scope timing, trace serialization and allocator attribution are
separate instrumentation facilities. [spdlog documentation](https://github.com/gabime/spdlog).

Native scope macros use RAII and a monotonic clock. C# uses disposable scopes or
Activity/EventSource adapters; Python uses context managers/logging adapters; Lua
callbacks/modules use native bridges plus LuaJIT sampling where appropriate.
All carry session/process/thread, category, asset/entity/widget and correlation IDs.
A shared session does not imply perfectly synchronized clocks across processes:
record clock-sync samples and uncertainty. Traces distinguish elapsed scopes,
sampled CPU stacks, waits, frame counters, allocations and memory snapshots.

Export a tested Chrome Trace Event JSON subset (complete duration, metadata,
counters, async flows), openable in a browser trace viewer. Keep event timestamps
at the producer, use microsecond units explicitly, escape strings and finalize
valid JSON on normal shutdown. Crash recovery uses bounded chunks with a recovery
index rather than promising an intact unfinished JSON document.
Perfetto supports legacy JSON on a best-effort basis; validate our exact subset,
with a future TrackEvent backend possible. [Viewer compatibility](https://perfetto.dev/docs/faq).

Build options are independent: RA_ENABLE_LOGGING, RA_ENABLE_PROFILING,
RA_ENABLE_MEMORY_TRACKING and RA_ENABLE_LUA_DEBUGGER. Native/MSBuild symbols remove
disabled code and argument evaluation. Python's shipped configuration skips
collection but cannot claim C++ compile-out semantics. Runtime capture controls
only exist when the corresponding capability was built in. Do not use a log sink
as an unbounded per-frame trace queue; define dropped-event counters, flush,
rotation, failure handling and bounded memory overhead.

Memory numbers have explicit semantics: stored/compressed bytes; estimated decoded
bytes; measured live allocations; reserved capacity; peak/high-water mark; process
private/working-set totals; native, managed, Lua heap and JIT code/cache categories.
GPU/device memory is a separate optional future category if a GPU backend exists.
Show object/component/asset allocations separately from allocator/VM/backend overhead.
A process total is not the sum of per-asset estimates.

Attribute native allocations through tagged allocators/resources and lifecycle
events, Lua through the VM allocator plus module/instance contexts, .NET through
runtime GC/allocation diagnostics plus logical ownership, and Python through
tracemalloc plus process metrics. Managed/native extension allocations not tracked
by those APIs remain explicitly unclassified. Shared VM heap and JIT code are not
arbitrarily assigned wholly to each entity.
[.NET EventPipe](https://learn.microsoft.com/en-us/dotnet/core/diagnostics/eventpipe),
[Python tracemalloc](https://docs.python.org/3/library/tracemalloc.html),
[LuaJIT statistical profiler](https://luajit.org/ext_profiler.html).

Allocation identity, VM/process identity and snapshot epoch drive de-duplication,
not just equal content hashes. Direct, inclusive unique, shared and incremental
costs are separate views; retained/reclaimable size is only reported when ownership
and rooting evidence support it. Count a shared script module once per VM, not
once per asset reference. Record observed dynamic require edges alongside declared
dependencies. Static analysis cannot know all dynamic module paths or future Lua
allocations. Capture timestamp, build/schema IDs, collector mode and sample limits;
never label partial capture as an exact explanation of all process RAM.

Diagnostic outputs are operational files, not authored gameplay bundles: save
logs/traces/snapshots outside source trees with retention limits, local-only by
default. They do not enter game bundles or commits. Schemas, reusable collectors,
tests and developer documentation are repository content.

**T01 - Shared telemetry schemas and cost terminology.** Depends: Q01.
Paths (4): `schemas/{log-event,trace-session,memory-snapshot}.schema.json`,
`docs/observability.md`.
Acceptance: units, IDs, unknown/unattributed fields, collector coverage and clock
domains are explicit; logs, profiling, sampling and memory metrics are distinct.

**T02 - Native logging API with spdlog.** Depends: T01, A04.
Paths (6): `engine/diagnostics/{CMakeLists.txt,include/raiders/Log.h,src/Log.cpp}`,
`cmake/Dependencies.cmake`, `CMakeLists.txt`, `tests/diagnostics/log_tests.cpp`.
Acceptance: structured context, bounded async queue, rotation and reliable shutdown;
compile-time level filtering; file/queue failures do not recurse into logging.

**T03 - Optional native profiling scopes.** Depends: T02.
Paths (5): `engine/diagnostics/{include/raiders/Profile.h,src/Profile.cpp}`,
`engine/diagnostics/CMakeLists.txt`, `cmake/Diagnostics.cmake`,
`tests/diagnostics/profile_toggle_tests.cpp`.
Acceptance: OFF build does not evaluate profile arguments or register scopes;
ON records nested/multithread scopes with producer timestamps and bounded buffers.

**T04 - Chrome JSON trace writer and capture sessions.** Depends: T03.
Paths (5): `engine/diagnostics/{include/raiders/TraceWriter.h,src/TraceWriter.cpp}`,
`engine/diagnostics/CMakeLists.txt`, `tests/diagnostics/trace_tests.cpp`,
`docs/observability.md`.
Acceptance: duration/counter/async events import in the chosen viewer; escaped
names, thread/process metadata, dropped counts and interrupted-session recovery.

**T05 - C# logging and profiling adapter.** Depends: T01, Q02.
Paths (6): `tools/dotnet/Raiders.Diagnostics/{Raiders.Diagnostics.csproj,
Log.cs,ProfileScope.cs,TelemetrySession.cs}`,
`tests/dotnet/Raiders.Diagnostics.Tests/{Raiders.Diagnostics.Tests.csproj,TraceTests.cs}`.
Acceptance: MSBuild profiling symbols, exception-safe scopes, structured logs and
session propagation; disabled instrumentation does not build expensive payloads.

**T06 - Python pipeline telemetry adapter.** Depends: T01, Q02.
Paths (4): `tools/pipeline/diagnostics/{logging.py,profiling.py,session.py}`,
`tests/pipeline/test_diagnostics.py`.
Acceptance: CLI stage/child-process timing, structured stderr/file sinks, bounded
outputs and disabled-collection tests; stdout protocol JSON remains uncontaminated.

**T07 - Instrument native execution and preview.** Depends: T04, B07, J07.
Paths (6): `engine/runtime/src/RunLoop.cpp`, `engine/render/src/WorldRenderer.cpp`,
`engine/physics/src/PhysicsWorld.cpp`, `apps/preview_host/PreviewApplication.cpp`,
`game/src/SpaceRaidersApplication.cpp`, `tests/diagnostics/runtime_trace_tests.cpp`.
Acceptance: event pump, fixed ticks, physics, extraction and presentation are
distinguishable; no per-object string formatting in disabled paths.

**T08 - Lua callback/module profiling bridge.** Depends: T03, G04.
Paths (5): `engine/scripting/{include/raiders/ScriptDiagnostics.h,
src/ScriptDiagnostics.cpp,src/ScriptSystem.cpp}`,
`engine/scripting/CMakeLists.txt`, `tests/scripting/diagnostics_tests.cpp`.
Acceptance: callback scopes, script IDs and correlation survive errors/coroutines;
Lua can log/profile without adding spdlog or file I/O to script APIs.

**T09 - Native tagged memory resources.** Depends: T01, C02.
Paths (6): `engine/diagnostics/{include/raiders/MemoryTracker.h,
src/MemoryTracker.cpp,include/raiders/AllocationTag.h}`,
`engine/diagnostics/CMakeLists.txt`, `tests/diagnostics/memory_tracker_tests.cpp`,
`docs/memory-model.md`.
Acceptance: allocation/free/realloc/reserved/peak accounting, tag nesting and tracker
overhead; native code outside tagged resources remains visibly unattributed.

**T10 - Lua allocator and JIT memory accounting.** Depends: T09, G01.
Paths (5): `engine/scripting/{include/raiders/LuaMemory.h,src/LuaMemory.cpp,
src/LuaRuntime.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/memory_tests.cpp`.
Acceptance: source/bytecode/module/instance/VM/cache categories, shared/unattributed
heap and collector coverage are separate; collection does not recursively allocate.

**T11 - .NET/Python process and heap diagnostics.** Depends: T05, T06, T09.
Paths (6): `tools/dotnet/Raiders.Diagnostics/{MemorySampler.cs,RuntimeEvents.cs}`,
`tools/pipeline/diagnostics/memory.py`,
`tests/dotnet/Raiders.Diagnostics.Tests/MemoryTests.cs`,
`tests/pipeline/test_memory_diagnostics.py`, `docs/memory-model.md`.
Acceptance: GC heap/allocations, process private/working set and tracemalloc metrics
are not summed twice; unavailable native-extension memory is marked unknown.

**T12 - Runtime memory snapshots and observed dependency edges.** Depends: T10, T11, R06.
Paths (6): `engine/diagnostics/{include/raiders/MemorySnapshot.h,
src/MemorySnapshot.cpp}`, `engine/diagnostics/CMakeLists.txt`,
`engine/assets/src/BundleStore.cpp`, `engine/scripting/src/ScriptResolver.cpp`,
`tests/diagnostics/snapshot_tests.cpp`.
Acceptance: player/UI -> components -> assets -> Lua modules traverses a stable
snapshot epoch; dynamic imports, shared allocations, cycles and missing tags are
reported; before/after load/unload identifies deltas without assuming a forced GC.

**T13 - Instrument every shipped application and pipeline stage.** Depends: T05, T06, J06, S01, W01.
Paths (7): the 4 C# application Program.cs files, `tools/assetc/src/main.cpp`,
`tools/pipeline/diagnostics/session.py` and `tests/integration/telemetry_apps_tests.py`.
Acceptance: every executable has session context, errors, CPU scopes and capture
controls; split by executable if registration or tests exceed the path limit.

**T14 - Coverage, disabled builds and overhead regression checks.** Depends: T07, T08, T12, T13, O03.
Paths (5): `tests/integration/{telemetry_coverage_tests.py,telemetry_cost_tests.cpp}`,
`CMakePresets.json`, `.github/workflows/ci.yml`, `docs/observability.md`.
Acceptance: logging/profile/memory capabilities tested ON/OFF; bounded-buffer
saturation, release shutdown and measured collector overhead; no fixed speedup claims.

**T15 - Capture control and snapshot/trace export in C#.** Depends: J06, T04, T05, T12.
Paths (5): `tools/dotnet/Raiders.Tools.Common/{CaptureController.cs,
CapturePanel.cs,DiagnosticsExport.cs}`,
`tests/dotnet/Raiders.Tools.Tests/CaptureTests.cs`, `docs/observability.md`.
Acceptance: start/stop/export from any editor/playtest, session correlation across
processes and clear unsupported-build feedback; inspector can open saved snapshots.
