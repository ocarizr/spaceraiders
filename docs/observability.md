# Shared telemetry contract (T01)

Version 1 defines data exchanged by native C++20, C#/.NET 10, Python and Lua
adapters. T01 defines schemas and terminology; T02 adds native logging and T03
adds optional elapsed-time scopes. T04 adds bounded Chrome JSON export/recovery.
T06 adds the installed Python pipeline adapter. Memory collectors and
cross-process clock conversion remain later tasks.

## Python pipeline adapter (T06)

`tools/pipeline/diagnostics` builds the `raiders-diagnostics` wheel for Windows
and Python 3.12, with no runtime dependencies. Source, tests and packaging belong
to that project. CI builds a wheel in a build venv, installs it with the pinned
pytest test extra in a separate execution venv, and exercises the installed
module and console entry point. The existing Code style job runs these tests;
its failure blocks CI gate. Impact-based job selection remains O01-O03.

```python
from raiders_diagnostics.logging import Log
from raiders_diagnostics.profiling import Profiler
from raiders_diagnostics.session import TelemetrySession

session = TelemetrySession.from_environment()
log = Log(session)  # stderr; pass a caller-owned UTF-8 writer for files
profiler = Profiler(session, capacity=1024, log=log)
with session.bind(correlation_id="cook.1", asset_id="asset.ship"):
    with profiler.stage(lambda: "validate-assets"):
        log.write("info", "assets", lambda: "Validation started", lambda: {"count": 12})
        # Perform pipeline work here.

events = profiler.drain()
```

Sessions use the v1 IDs and monotonic microsecond clock. Process/clock IDs are
generated once per interpreter process and thread IDs once per thread instance.
`bind` inherits omitted optional IDs; `None` clears one and scope exit restores
the previous context, including on exceptions. Context variables isolate async
tasks. New threads require explicit `contextvars.copy_context().run(...)` when
inheritance is desired; the context object must not be entered concurrently.
Scopes capture context at entry. Use `with profiler.stage(...)` inside async
functions too; its duration includes suspension and waits.

`from_environment()` accepts `RA_TELEMETRY_SESSION_ID` and optional
`RA_TELEMETRY_CORRELATION_ID`, `RA_TELEMETRY_ASSET_ID`, `RA_TELEMETRY_ENTITY_ID`,
`RA_TELEMETRY_WIDGET_ID`, `RA_TELEMETRY_VM_INSTANCE_ID`.
`RA_ENABLE_LOGGING` and `RA_ENABLE_PROFILING` independently accept `0` or `1`
(default `1`). `child_environment()` copies a base environment, clears stale
optional context and passes the active IDs and switches. Child programs opt in
by reading this contract; native/managed programs need their own environment
integration. Process and clock identities are never inherited. These values
correlate events; they do not synchronize clocks across processes.

`Log.write` emits v1 JSONL to stderr by default, never implicitly to stdout.
Levels run from trace through critical; `minimum="off"` rejects everything.
Message/attribute factories execute only after collection, severity and record
budget checks. Python eagerly evaluates normal expressions: use factories for
expensive optional payloads. Each logger serializes writes, bounds lifetime
records and canonical UTF-8 bytes per record (including LF), and exposes
`written`, `rejected`, `failures`, `faulted` stats. Defaults are 10,000 records
and 16,384 bytes each. Files should use UTF-8 and `newline="\n"`; caller-owned
writers that translate newlines can add bytes. Share one logger per sink.
Unsafe JSON numbers, malformed payloads and oversized records never reach the
sink. Factory/serialization errors return false and increment failures;
filtering/budget/size rejections increment rejected. Sink failure permanently
faults the logger to avoid repeated partial writes. A failed write can already
have emitted a partial record: discard/recover that capture before reuse.
The caller owns disposal/retention. Serialization and user factories can allocate
before the byte limit is checked; the adapter does not cap their transient heap.

`Profiler` reserves capacity for both active scopes and completed records.
Its stats expose capacity, buffered/in-flight, completed, dropped, rejected and
accepting. Full buffers drop new scopes without evaluating their name factories.
`stop()` rejects new scopes while allowing active ones to complete; `drain()`
removes completed events and releases their slots. OFF skips names, clocks and
buffers and reports zero capacity. Invalid names/factory failures reject the
scope while still executing its body. Work exceptions propagate after recording
an error outcome. Events contain name/category, entry context, start/duration
in microseconds and outcome. These are elapsed-time scopes, not CPU samples or
heap measurements; Python heap diagnostics remain T11. Events are returned in
completion order; nested durations overlap and must not be summed as CPU time.
Optional log summaries use category `pipeline.profile`. Full trace manifests,
Chrome export and cross-process alignment remain future integration work.

`Profiler.run([executable, ...], timeout=seconds)` measures a child process,
uses no shell and inherits stdout/stderr without buffering output in memory.
It passes the session environment and propagates nonzero exit/timeout exceptions.
A timeout kills and waits for the direct child; descendant-tree lifecycle and
job-system integration remain V18. Command lines/environment values are not
copied into telemetry. Child stderr may contain ordinary diagnostics; use a
dedicated file sink when the telemetry stream must contain only JSONL.

The installed CLI wraps a pipeline stage and its child timing:

```powershell
python -m venv .venv/diagnostics-build
python -m venv .venv/diagnostics
.venv/diagnostics-build/Scripts/python.exe -m pip wheel --no-deps --wheel-dir .tools/diagnostics/wheels tools/pipeline/diagnostics
.venv/diagnostics/Scripts/python.exe -m pip install '.tools/diagnostics/wheels/raiders_diagnostics-0.1.0-py3-none-any.whl[test]'
.venv/diagnostics/Scripts/python.exe -m pytest -c tools/pipeline/diagnostics/pyproject.toml tools/pipeline/diagnostics/tests -v
.venv/diagnostics/Scripts/raiders-telemetry.exe --log-file out/pipeline.jsonl --timeout 60 -- .venv/diagnostics/Scripts/python.exe -c "print('{}')"
```

Check each command's exit code before continuing. Log files are created exclusively
and never overwritten; create parent directories first. `--disabled` overrides
both switches and creates no telemetry file. Disabled collection still executes
the child and preserves its stdout. The CLI returns the child exit code, `124`
on timeout and `2` for invalid configuration/launch errors. The wrapper does not
validate or rewrite the child's stdout protocol.

## Managed logging and profiling adapter (T05)

`tools/dotnet/Raiders.Diagnostics` is a .NET 10 library/NuGet package with no
third-party runtime dependencies. Its MSTest project lives inside the owning
project's `tests/` directory. Central package pins and both `packages.lock.json`
files are committed; CI restores in locked mode and requires managed diagnostics
alongside native tests and formatting in `CI gate`.

`TelemetrySession` takes the coordinator's session ID. Process and clock-domain
identities are generated once per process; thread-instance identities and event
IDs are unique within that process. `PushContext` uses `AsyncLocal` to propagate
correlation/asset/entity/widget/VM IDs through async work. Dispose context leases
in reverse order in the flow that created them. Omitted IDs inherit the outer
context; disposing restores it. For child processes, explicitly pass the session
and correlation IDs; this library does not launch processes or synchronize clocks.

`Log.Write` emits the v1 JSONL envelope to a caller-owned `TextWriter`. Choose a
UTF-8 `StreamWriter` for files or `Console.Error` for stderr; it never selects
stdout implicitly. Message and attribute factories execute only for accepted
severity/budget checks. Writes are synchronous and serialized per logger; share
one logger per sink, or explicitly synchronize a sink used by multiple loggers.
Configure minimum
severity, lifetime record count and UTF-8 record byte limit (including newline).
Filtered/full/oversized/unsafe-number records increment `Rejected`; callback,
serialization and sink errors increment `Failures` and return false without
recursive logging. A sink failure permanently stops that logger, preventing
repeated partial writes; create a new logger after repairing the sink. The caller
owns writer disposal and file retention. Payload construction/serialization has
temporary allocation cost before the final byte limit can be checked.

Profiling is elapsed time, including waits, measured with `Stopwatch` relative to
a process-wide origin. Microseconds are truncated from elapsed ticks. Enable the
MSBuild property `RaidersEnableProfiling=true` in both the library and caller;
it defines `RA_ENABLE_PROFILING`. A NuGet consumer must define this symbol in its
own project too: conditional calls are erased by the **caller's compiler**.
Build/package the library with profiling enabled to retain capture support.

```csharp
var session = new TelemetrySession("capture.1");
using var context = session.PushContext(correlationId: "command.1");
ProfileScope scope = default;
ProfileScope.Begin(ref scope, session, "editor", BuildExpensiveScopeName());
using (scope)
{
    PerformEditorWork();
}
```

Begin the mutable local before entering `using`; do not initialize it inside a
readonly using declaration. OFF removes the entire `Begin` call and argument
evaluation; disposing the default value does nothing. ON captures immutable
context and producer timestamps. Disposal during exception unwinding completes
the scope; copied handles complete at most once. Scopes are synchronous: do not
hold them across `await` or dispose them on another thread. For async work,
propagate context and instrument synchronous segments; cross-thread disposal is
rejected and releases its reserved slot.

Capacity includes both buffered events and active reservations. Overflow increments
`Dropped`; `Stop` rejects new scopes while existing scopes can complete. `Drain`
returns/removes completed events. OFF reports zero collector capacity and no
acceptance. `Stats` reports capacity, buffered/in-flight counts, completions,
drops and rejections. These are instrumented scopes, not CPU samples or heap
measurements. Managed trace manifests, viewer export and capture UI belong to T15;
the adapter does not claim a complete Chrome trace or cross-process alignment.

From a Windows checkout with the SDK selected by `global.json`:

```powershell
$project = 'tools/dotnet/Raiders.Diagnostics/tests/Raiders.Diagnostics.Tests.csproj'
dotnet restore $project --locked-mode
dotnet test $project --no-restore -c Debug -p:RaidersEnableProfiling=true
dotnet test $project --no-restore -c Debug -p:RaidersEnableProfiling=false
dotnet test $project --no-restore -c Release -p:RaidersEnableProfiling=true
dotnet test $project --no-restore -c Release -p:RaidersEnableProfiling=false
dotnet pack tools/dotnet/Raiders.Diagnostics/Raiders.Diagnostics.csproj --no-restore -c Release -p:RaidersEnableProfiling=true -o out/packages
```

Every nonzero exit code fails validation. CI executes the same four combinations
in one Windows job, adds `-- RunConfiguration.TreatNoTestsAsError=true` to reject
empty suites, and records TRX results. Packing produces a local NuGet
artifact; publishing is a separate operation.

## Chrome JSON export and recovery (T04)

`raiders/TraceWriter.h` exposes a synchronous consumer-side writer on
`raiders_diagnostics`. Enable `RA_ENABLE_PROFILING=ON` to build it. The writer uses
the already pinned nlohmann/json dependency even if logging is OFF; spdlog remains
logging-only. Profiling/logging both OFF fetch neither dependency. The scope
collector still performs no JSON serialization or file I/O.

```cpp
#include "raiders/TraceWriter.h"

std::filesystem::create_directories("out/captures");
raiders::TraceConfig config;
config.directory = "out/captures/session-1"; // New directory; parent must exist.
config.session_id = "capture.1";
config.process_instance_id = "game.1";
config.clock_domain_id = "steady.game.1";
config.build_id = "build.1";
config.process_name = "Space Raiders";
raiders::TraceWriter writer(config);
raiders::ProfileCapture capture;
{
    RA_PROFILE_SCOPE(capture, "runtime", "update");
}
capture.Stop(); // End/join all scopes/producers before the final drain.
for (const auto& event : capture.Drain()) writer.AppendScope(event);
writer.ObserveProfile(capture.Stats());
writer.Counter(1, "queue", "pending", "queue.count", 7);
writer.AsyncBegin(1, "assets", "load", "load.1", {"command.1"});
writer.AsyncEnd(2, "load.1");
if (!writer.Finalize()) { /* report failure through an independent control path */ }
```

Call writer methods from a drain/control thread: they allocate, serialize and
write synchronously. Methods are mutex-serialized; stop/join callers before
destruction. Scope events retain their producer timestamps and duration.
Counter/async timestamps are sampled on their calling producer before the writer
lock. Their thread IDs must use the same logical identity namespace as scope
events; the numbers in the example are illustrative. Pass only events from the
configured process and steady-clock domain. Each writer represents one process;
it does not synchronize or merge other processes' clocks.

Output includes the T01 session envelope, process/clock/collector declarations,
`X` complete durations, `C` counters, `M` process/thread names and `b/e` nestable
async pairs. `RegisterThread()` supplies labels; unknown labels are generated as
`thread.<id>`. Text is UTF-8, limited to 128 bytes and escaped by the serializer.
Context IDs appear in scope/async args; empty optional IDs are omitted. Counter
series require `.bytes`, `.count`, `.us` or `.ratio` suffixes and finite values.
Async IDs must be unique among active operations; ends inherit the begin's name/
category/context and may arrive from another thread. Unknown ends, duplicate begins,
malformed fields and regressing/incompatible cumulative profile stats are rejected.
Flow arrows (`s/t/f`), async instants and CPU sample export are outside this writer's
implemented subset, although the shared schema reserves those phases.

Observe cumulative stats from exactly one profile collector after each drain.
Final scope coverage is partial if the collector was not observed, is still
accepting, has in-flight/undrained records, or its completed count differs from
the number handed to the writer. Producer drops/rejections and writer losses also
make coverage partial. Counter/async coverage describes the writer's own calls,
not all activity in the application. Conservative writer loss counts are shared
across signal descriptions and must not be summed. Exported counts saturate at
the schema's exact-integer ceiling; at that ceiling they are lower bounds.
`Finalize()` success means publication succeeded, not that coverage is complete.

Buffers/storage are bounded by `events_per_chunk` (default 128, maximum 1024),
`max_chunks` (default 32, maximum 256), 4096 serialized bytes per event,
`max_threads` and `max_async` (default 128 each, maximum 1024). Metadata consumes
the same chunk budget. A full capture drops new records; it does not rotate away
already committed history. Pending JSON DOM/serialization overhead is additional
to serialized sizes but bounded by those limits. Finalization/recovery materializes
and sorts the bounded retained history, so it can use substantially more memory
than the pending chunk. It is not intended to run in a frame's instrumented scope.
Begin another capture when the configured retention budget is exhausted.

`Checkpoint()` writes a complete `chunk-N.json`, then publishes a checksummed
`recovery-index.json` via same-directory temporary-file rename. The index records
version 1, limits, manifest, canonical chunk names and CRC32 checksums. CRC32 detects
accidental corruption; it is not authentication for hostile captures. A Windows
exclusive `capture.lock` handle prevents recovery while the writer is alive.
The constructor refuses any pre-existing capture directory, preserving user files.

`Finalize()` checkpoints, sorts events by time (outer duration first for equal
start times), preserves async source order at equal timestamps and publishes
`trace.json`. It is idempotent and refuses to overwrite an existing output.
Unfinished async begins and overlapping nonnested complete scopes are omitted
and reported through partial coverage/drop counts, so ambiguous slices are not
silently presented as valid nesting. Check `TraceStats` and the exported coverage;
writer `accepted` includes metadata, while `rejected`/`dropped` describe ingress
loss and do not include the export-time omissions. I/O failures latch the writer
into a failed state and make subsequent checkpoint/finalization fail. No error
path logs recursively. Initialization errors throw; other operations report false.

After an interruption, call `TraceWriter::Recover(directory)` from a separate
control process or after the original writer has been destroyed. Recovery reads
only the last published index and complete indexed blocks, checks sizes/checksums,
rejects noncanonical names/reparse-point files and writes `recovered.json` without
overwriting it. Orphan blocks and `.tmp` files are ignored. All recovered coverage
is partial: records since the last checkpoint and exact crash-time drop counts
are unknown. The destructor attempts a checkpoint, but does not finalize a trace;
the crash test terminates a child process without allowing that destructor to run.
Filesystem flush/atomic rename is not a promise of durability through power loss,
storage-device failure or every network filesystem implementation.

With profiling OFF, writer methods return false and create no capture directory.
The stub still has a small implementation object; only scope macros promise full
argument removal. Tests live in `engine/diagnostics/tests/trace_tests.cpp` and cover
the emitted phases, escaping, metadata, limits, producer drops, active-capture
locking, abrupt child-process recovery, corrupt blocks and I/O failure.

Viewer validation uses Windows Perfetto Trace Processor v58.2
(`add693d8b338ba9599dbcbc3e300b1ab8c000897`), from the
[official launcher](https://get.perfetto.dev/trace_processor). Its Windows binary
SHA-256 is `adfa6bad3d72be3ba9b83fa2b17b69fa13b3ab1cad0f42e52b86188bd5f0f997`.
Generated normal/recovered files are checked against T01 and imported with
PerfettoSQL queries for duration/async slices, counter values and process/thread
names, with no nonzero parser error statistics. This validates the chosen viewer's
parser; it is not a visual layout review of the web UI. Open `trace.json` or
`recovered.json` through **Open trace file** at [Perfetto UI](https://ui.perfetto.dev).
Perfetto's legacy JSON support is
[best effort](https://perfetto.dev/docs/faq); recheck the exact subset when changing
exporter/viewer versions. Viewer binaries, captures and verification helpers stay
local, not in this repository or the shipped game.

## Native profiling scopes (T03)

The `raiders_diagnostics` target also exposes `raiders/Profile.h`. Scope collection
is independent of `RA_ENABLE_LOGGING` and uses no spdlog/JSON itself. T04's writer
adds the JSON dependency to profiling-enabled builds.
`RA_ENABLE_PROFILING` defaults to OFF and is propagated to consumers through
`cmake/Diagnostics.cmake`. Use a separate build directory when changing modes:

```powershell
cmake --preset windows-debug -B out/build/profile-debug -DRA_ENABLE_PROFILING=ON
cmake --build out/build/profile-debug
ctest --test-dir out/build/profile-debug --output-on-failure --no-tests=error

cmake --preset windows-release -B out/build/profile-only -DRA_ENABLE_PROFILING=ON -DRA_ENABLE_LOGGING=OFF
cmake --build out/build/profile-only
ctest --test-dir out/build/profile-only --output-on-failure --no-tests=error
```

```cpp
#include "raiders/Profile.h"

raiders::ProfileCapture capture(4096);
{
    RA_PROFILE_SCOPE(capture, "runtime", "update");
    { RA_PROFILE_SCOPE(capture, "physics", "step"); }
}
capture.Stop();
auto completed = capture.Drain();
auto stats = capture.Stats();
```

An ON scope copies name/category and optional correlation, asset, entity, widget
and VM IDs into fixed 129-byte fields (128 UTF-8 bytes plus terminator). Empty
optional IDs mean unknown/not applicable; exporters must translate them to absent/
null T01 context fields. Invalid UTF-8, embedded NUL, overlong text, empty names/
categories and invalid IDs are rejected, not silently truncated. Context views are
copied at entry and need not outlive the scope. Scopes cannot be copied or moved;
they must end on their starting thread, including during exception unwinding.
Cross-thread destruction is rejected and frees the reserved slot.

Each capture preallocates event slots and free/completed index storage. Capacity
includes both in-flight and completed-but-undrained scopes. A full capture drops
new scopes and increments `dropped_full`; existing scopes keep their slots. Scope
entry/exit does not allocate collector memory, format strings, perform I/O or call
logging. It does use a mutex and reference-counted state; it is not lock-free.
Caller argument construction is outside the collector bound. Collector storage is
approximately `capacity * (sizeof(ProfileEvent) + 2 * sizeof(size_t))`, plus vector,
mutex and shared-state overhead; it is not a measurement of total process RAM.

Start/end readings occur on the producer thread, before collector-lock acquisition,
using the same truncated steady-clock microsecond epoch as `Log::ClockNowUs()`.
Durations measure elapsed time, including waits, rather than sampled CPU time.
Logical thread IDs are assigned once per thread lifetime by a module-local sequence
and are not reusable OS thread IDs. One linked diagnostics module per process is
the identity boundary, as with logging. Session/process/clock manifest association
is supplied by the T04 writer; T03 does not serialize trace events.

`Drain()` returns only completed records in completion order and releases their
slots for reuse; nested scopes naturally complete inner-first. The returned vector
is a consumer allocation outside the collector's retained buffer. It may throw on
allocation failure without discarding buffered records. A writer must preserve
timestamps/nesting rather than treating completion order as start-time order.
`Stop()` is idempotent and prevents new registrations; it does not wait for active
scopes. Existing scopes retain shared capture storage and complete safely even if
the owner is destroyed. Stop/join producers and end scopes before the final drain
when events must be exported; destroying a capture is not an implicit export.
The capture object must remain alive while new scope constructors refer to it.

`accepted` counts reserved scopes; `completed` counts records made drainable.
`rejected` includes invalid/stopped starts and invalid completions. `in_flight`,
`buffered` and `capacity` describe the current collector occupancy. Drops/rejections
must be reflected in partial coverage by a future exporter, not treated as zero
profiling overhead or complete capture.

In OFF builds the macro removes all argument expressions, declarations and scope
registration. Collector/scopes are inert stubs with no allocated state. Direct
constructor calls still evaluate caller arguments; use the macro for compile-out.
The default native CI executes the OFF checks. Explicit ON Debug/Release checks,
including logging OFF, exercise nesting, threads, exception unwinding, bounded
storage and capture lifetime. Persistent CI coverage of the build-mode matrix
belongs to T14; trace export/viewer checks belong to T04.

## Native logging API (T02)

`engine/diagnostics` now provides the independently linkable C++20 target
`raiders_diagnostics` and public header `raiders/Log.h`. Its tests live inside
`engine/diagnostics/tests`. The legacy game is not instrumented by this task.
spdlog 1.17.0 handles file output/rotation; nlohmann/json 3.12.0 handles strict UTF-8
serialization. CMake fetches both from versioned archives with SHA-256 checks in
`cmake/Dependencies.cmake`. Third-party headers do not appear in the public API.
Cold logging-enabled configuration needs network access; an existing populated
FetchContent tree or explicit source-directory overrides can support offline work.

```cpp
#include "raiders/Log.h"

raiders::LogConfig config;
config.path = "out/captures/game.jsonl";
config.session_id = "capture.1";
config.process_instance_id = "game.1";
config.clock_domain_id = "steady.game.1";
raiders::Log log(config);
raiders::LogContext context;
context.thread_id = "main.1";
RA_LOG_INFO(log, "assets", context, "Source bundle opened");
log.Shutdown();
const auto stats = log.Stats();
```

Supply stable session/process IDs and a clock domain for the actual producer.
`Log::ClockNowUs()` reads `steady_clock::time_since_epoch()` truncated to integer
microseconds; there is no per-logger clock reset or cross-process synchronization.
Missing thread/context IDs remain null. The caller owns session manifest export;
this library writes only T01 log events, one UTF-8 JSON object per line. Attributes
support null, bool, exact signed JSON integers, finite doubles and UTF-8 strings;
nested arbitrary JSON is deliberately outside this first API. Event IDs are
generated by a module-wide atomic sequence; use one linked diagnostics module per
process, or distinct process-instance identities for independently loaded copies.

The project owns a bounded drop-newest queue and one consumer thread per `Log`.
That worker feeds a private synchronous spdlog logger/rotating sink. It does not
use spdlog's default registry or global async thread pool. Producers serialize
under a queue mutex, never wait for disk or queue space, and capture timestamps
before that mutex. They can contend on serialization; this is not a lock-free API.
Queue capacity defaults to 1024 records, serialized record size to 16 KiB, and
retention to a 1 MiB active file plus three backups. One dequeued record and bounded
serialization/backend scratch space exist in addition to queue storage. Caller
argument construction/allocation is outside that bound. Validate configured memory
budgets before using large capacities in a product.

Full queues increment `dropped_full`; oversize/invalid records, allocation failures
and writes after shutdown increment `rejected`. Runtime-filtered records do not
increment either counter. Serialization rejects invalid UTF-8, invalid IDs,
duplicate/empty attribute keys, nonfinite numbers and integers outside the exact
JSON range. Attributes are limited to 64 keys of at most 128 bytes; categories to
128 bytes. Inputs and the serialized result are checked against the record budget.
Rotation keeps whole JSONL records and may intentionally discard old files under
the retention policy. Do not point two logger instances/processes at the same file.

`Flush()` is a barrier for records accepted at entry and reports whether any sink
errors have occurred. It is not an `fsync`/crash durability promise. `Shutdown()`
stops acceptance, drains accepted records, flushes, joins the worker and closes the
file. It is idempotent and serialized against `Flush()`; concurrent writes are
either accepted before shutdown or rejected. Stop producers before destroying the
object. The destructor performs shutdown as a safety net. Shutdown can wait for
slow filesystem operations; it cannot guarantee a hard deadline or process-crash
delivery. `processed` counts attempted records, not guaranteed successful writes.

Configuration/file-open failures throw to the caller. Later sink/rotation errors
increment `write_errors`; the backend error handler only updates an atomic counter
and never calls logging recursively or emits another record. Check counters through
an independent control path; nonzero drops/errors require partial capture coverage.
No automatic stderr fallback or hidden unbounded emergency queue is installed.

`RA_ENABLE_LOGGING=OFF` builds the API stubs without fetching/linking the backends,
opening files or starting workers. `RA_LOG_ACTIVE_LEVEL` is a CMake integer from
0 (trace) through 5 (critical), or 6 (off); default 2 includes info and above.
The six `RA_LOG_*` macros remove disabled calls, including evaluation of logger,
context, attributes and message expressions. Put formatting inside the macro call.
Direct `Write()` calls evaluate their arguments and only apply the runtime minimum
level; they do not promise compile-time filtering. Profiling/memory/debugger flags
remain independent future facilities.

Build the default target and run CTest in both native presets to include logging
tests. They cover structured values/escaping, producer timestamps, rejected input,
concurrent producers, drain/flush, retention, locked-file rotation failure, bounded
queue saturation and disabled-macro argument evaluation. A separate logging-OFF
configuration verifies the stub build without dependency downloads. Viewer and
trace export remain T03/T04; this task does not export a trace/session manifest.

## Files and compatibility

| Contract | Storage and purpose |
| --- | --- |
| [Log event](../schemas/log-event.schema.json) | One JSON object per UTF-8 JSONL line; discrete diagnostic messages |
| [Trace session](../schemas/trace-session.schema.json) | One finalized JSON object; producer/clock/coverage manifest, timed events and statistical samples |
| [Memory snapshot](../schemas/memory-snapshot.schema.json) | One JSON object per process and snapshot epoch; measurements, estimates and attribution graph |

Schemas use [JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12/json-schema-core).
Register all three locally by their `$id` before validation; the `.invalid` URLs
are stable identifiers, not download endpoints. Shared definitions live in the
trace-session schema so logs and snapshots use the same context and coverage.
Validate with network resolution disabled. Unknown envelope fields and unsupported
`schema_version` values fail validation. Extensible log attributes and trace args
must remain JSON-compatible; NaN and infinity are forbidden. Breaking field/unit/
meaning changes require a new version and schema IDs, not reinterpretation of v1.

Structural validation cannot prove reference integrity, deduplication, clock
conversion, event ordering or allocator completeness. Consumers must also apply
the semantic rules below and reject inconsistent captures with diagnostics.

## Identity, context and time

IDs are opaque, case-sensitive ASCII strings of 1-128 characters. A session ID
identifies a capture across cooperating processes; it is not a process lifetime.
Generate a distinct `process_instance_id` for each launch, even if Windows reuses
its PID. `build_id` identifies immutable producer code/configuration; the top-level
build identifies the capture coordinator and each process lists its own build.
Asset/entity/widget IDs refer to the owning system's stable IDs. Include entity
generation and VM lifetime in IDs where reuse is possible. Addresses and content
hashes alone are not allocation or instance identities.

Every log/sample/snapshot context identifies its session, process and clock domain.
`thread_id` is a string instance identity, or null when unknown/not applicable.
Optional correlation/asset/entity/widget/VM IDs may be omitted or null; neither
means zero or an invented owner. Required identity, timestamp or clock fields
cannot be unknown: report collection failure instead of fabricating them.
Log `event_id` is unique within its process instance; transport retries preserve it.
Propagate correlation IDs through commands, callbacks and child processes.

Timestamps, elapsed durations, clock resolution, uncertainty and sample weights
use **microseconds**, memory uses **bytes**, and counts are dimensionless.
Integer values are limited to 0..9007199254740991 for exact JSON interoperability;
reject overflow rather than round it silently. A producer converts its monotonic
clock into integer microseconds with a documented rounding rule. Resolution can
be fractional microseconds; null means it was not determined. Clock source names
identify the actual source, such as Windows QPC or a runtime monotonic adapter.
Wall-clock UTC, if added as an attribute for human display, is not the scope clock.

Clock IDs are unique within a session. Equal session IDs do not synchronize clocks.
Each sync sample pairs readings from source/target domains and records a measured
nonnegative uncertainty bound. An empty sync list means no cross-domain alignment
was established. Exported `traceEvents.ts` values must already be converted into
the session's `clock_domain_id`. Do not merge unaligned events into that timeline;
retain separate sessions/domains and report partial coverage. Keep original
readings and conversion evidence in scope args when conversion is performed.
Samples, logs and snapshots retain the domain in their own context.

The session process registry maps unique viewer `trace_pid` values to process
instances; OS PID is informational and nullable. Event `tid` values identify
threads within a trace process, not globally. Emit `thread_name` metadata with the
runtime thread instance ID in args when a mapping is known. All contexts/process
references and domain references must resolve to the associated session manifest.

## Signals and coverage

| Signal | Meaning and limits |
| --- | --- |
| Logs | Severity/category/message/context; a timestamped message does not measure CPU work |
| Instrumented scopes | Elapsed duration, including waits/descheduling; not necessarily CPU time |
| Counters | Numeric observations at a timestamp; each series name ends in `.bytes`, `.count`, `.us` or `.ratio` |
| Async/flow events | Correlate work across callbacks/threads; causal links do not establish clock synchronization |
| Statistical samples | Root-to-leaf stack observations with sampling weight in microseconds; not a complete execution history |
| Memory snapshots | Point-in-time/interval-qualified quantities and ownership evidence; not timing events |

The trace schema allows complete scopes `X`, process/thread naming metadata `M`,
counters `C`, nestable async `b/n/e`, and flows `s/t/f`. `X` requires nonnegative
`dur`; async/flow events require a string `id`. Counter args contain only numeric
series values. Missing counter observations are omitted and reflected in coverage,
never represented by a fake zero. Scope/async/flow args may carry context IDs.
Async identity is scoped by category, name, ID and process; cross-process causality
uses the shared correlation ID. Async and flow sequences must start/end correctly;
complete scopes must nest consistently on a thread. Metadata names are limited
to `process_name` and `thread_name` in v1.

The `traceEvents` array targets a Chrome Trace Event JSON subset; extra session
fields and `samples` are our envelope. Samples need a separate adapter for viewer
visualization. T01 does not claim a tested Chrome/Perfetto import: T04 verifies the
exact exporter subset and escaping. See Perfetto's
[external trace format guidance](https://perfetto.dev/docs/getting-started/other-formats).

Every collector declares its process, signal, method/version and coverage. List
disabled/unavailable collectors explicitly rather than treating absence as success.
`complete` means complete only for the stated scope/method/interval, never all
process activity, and requires zero dropped records. `partial`, `disabled` and
`unavailable` require an explanation. Unknown dropped count/capacity/interval is
null; zero capacity is not an unknown marker. A full queue or interrupted capture
is partial, with known drops counted. Record limits in the scope description and
capacity field. A sampling interval does not turn samples into exact CPU accounting.

Example log (the named session/process/clock must exist in a session manifest):

```json
{
  "schema_version": 1,
  "event_id": "log.1",
  "timestamp_us": 100,
  "context": {"session_id": "capture.1", "process_instance_id": "game.1", "thread_id": "main.1", "clock_domain_id": "qpc.1", "entity_id": null},
  "severity": "info",
  "category": "assets",
  "message": "Source bundle opened",
  "attributes": {"bundle_id": "bundle.sample"}
}
```

Minimal session example with one scope and no sampling collector enabled:

```json
{
  "schema_version": 1,
  "session_id": "capture.1",
  "build_id": "build.1",
  "content_mode": "serialized-editing",
  "clock_domain_id": "qpc.1",
  "clock_domains": [{"id": "qpc.1", "source": "Windows QPC", "monotonic": true, "resolution_us": 0.1}],
  "processes": [{"process_instance_id": "game.1", "trace_pid": 1, "os_pid": 1200, "name": "SpaceRaiders", "language": "cpp", "build_id": "build.1"}],
  "clock_sync": [],
  "collectors": [
    {"id": "logs.1", "process_instance_id": "game.1", "signal": "logs", "method": "structured-log/v1", "coverage": {"status": "complete", "scope": "Startup logs, capture interval 0..200 us", "reason": null, "dropped_records": 0, "capacity_records": 1024, "sampling_interval_us": null}},
    {"id": "scope.1", "process_instance_id": "game.1", "signal": "scopes", "method": "native-raii/v1", "coverage": {"status": "complete", "scope": "Instrumented startup scopes, capture interval 0..200 us", "reason": null, "dropped_records": 0, "capacity_records": 1024, "sampling_interval_us": null}},
    {"id": "memory.1", "process_instance_id": "game.1", "signal": "memory", "method": "tagged-native/v1", "coverage": {"status": "partial", "scope": "Tagged native live allocations at epoch 1", "reason": "Untagged allocations and VM heaps are not collected", "dropped_records": 0, "capacity_records": 1024, "sampling_interval_us": null}},
    {"id": "sampling.1", "process_instance_id": "game.1", "signal": "samples", "method": "none", "coverage": {"status": "disabled", "scope": "CPU stack sampling", "reason": "Not built in this example", "dropped_records": 0, "capacity_records": null, "sampling_interval_us": null}}
  ],
  "traceEvents": [
    {"name": "startup", "cat": "runtime", "ph": "X", "pid": 1, "tid": 1, "ts": 100, "dur": 20, "args": {}},
    {"name": "thread_name", "cat": "runtime", "ph": "M", "pid": 1, "tid": 1, "ts": 0, "args": {"name": "Main", "thread_instance_id": "main.1"}}
  ],
  "samples": []
}
```

## Memory cost and attribution

Every metric has a quantity, a basis (`measured`, `estimated`, `unavailable`),
`value_bytes` and reason. Unavailable values require null and a nonempty reason;
measured/estimated values require a nonnegative byte count. A measured zero is
valid and is different from unknown. Give estimates their model/assumptions in
reason. `decoded` is an estimate unless actually measured. Never label modeled
player/UI memory as measured resident RAM.

| Quantity | Interpretation |
| --- | --- |
| `stored`, `compressed` | Serialized/uncompressed and compressed storage sizes; not live RAM |
| `decoded` | Decoded representation; excludes unrelated VM/allocator overhead unless stated |
| `live-allocated` | Currently live tracked allocations, excluding freed records |
| `reserved-capacity` | Capacity held by a container/allocator; may contain live bytes |
| `peak-live` | Maximum live allocated bytes over the stated observation interval |
| `process-private`, `working-set` | Windows process observations; overlapping views, not additive |
| `retained`, `reclaimable` | Only with rooting/ownership/GC evidence and a stated policy; otherwise unavailable |

Snapshot `metrics` are process-level observations. Allocation records describe
tracked blocks/aggregates; process totals and attribution totals must not be added.
Do not add reserved, live and peak as if disjoint. Every metric's method, scope,
time interval and limitations belong in coverage/reason. Estimated untracked bytes
are not computed as private bytes minus arbitrary allocator totals.

An allocation key is `(process_instance_id, vm_instance_id, allocation_id, epoch)`.
IDs include an allocation generation so reuse is distinguishable. A null VM means
no VM is known/applicable; it does not merge multiple VMs. Snapshot epoch increases
per process and never implies an atomic cross-process or stop-the-world snapshot.
Report races/partial enumeration in coverage. Never compare different content modes
as if they were identical: editing includes XML/parser/deserialized data, while
packaging-debug and shipping may use cooked/compiled/mapped data.

Owners form a logical graph of entities, components, widgets, assets, scripts,
VMs, processes and collector overhead. References distinguish ownership from
non-owning dependencies, and declared edges from observed dynamic `require` edges.
Owner IDs must be unique; all edge endpoints and allocation owner IDs must exist.
Reference cycles are allowed and traversal uses a visited set. Ownership cycles
are invalid for tree-style attribution and must be diagnosed. Empty `owner_ids`
means unattributed allocation; `unclassified` means its allocation category is
unknown. These are separate conditions. Missing tags/native extensions and shared
VM/JIT overhead remain visible instead of being guessed into a player or UI.

Direct cost uses allocations attributed solely to an owner; shared cost shows
allocations with multiple owners. Inclusive unique cost traverses the graph and
unions allocation keys before summing. Incremental cost is the measured before/
after delta for a stated load/unload experiment, not automatically reclaimable
size. Shared Lua modules are counted once per VM/allocation, not per referencing
entity, and equal content hashes do not prove shared physical allocation.
Collector allocations use category `collector` and remain part of process RAM.
GPU/device memory is outside this version's accounting, not a hidden zero.

Partial memory example with an unknown process metric and one shared allocation:

```json
{
  "schema_version": 1,
  "snapshot_id": "snapshot.1",
  "epoch": 1,
  "build_id": "build.1",
  "content_mode": "serialized-editing",
  "timestamp_us": 150,
  "context": {"session_id": "capture.1", "process_instance_id": "game.1", "thread_id": null, "clock_domain_id": "qpc.1"},
  "coverage": {"status": "partial", "scope": "Tagged native live allocations at epoch 1", "reason": "Untagged allocations and VM heaps are not collected", "dropped_records": 0, "capacity_records": 1024, "sampling_interval_us": null},
  "metrics": [{"quantity": "process-private", "basis": "unavailable", "value_bytes": null, "reason": "Process metrics collector disabled"}],
  "allocations": [{"allocation_id": "block.1.gen1", "vm_instance_id": null, "category": "native", "owner_ids": ["asset.player", "asset.ui"], "metrics": [{"quantity": "live-allocated", "basis": "measured", "value_bytes": 4096, "reason": null}]}],
  "owners": [{"owner_id": "asset.player", "kind": "asset", "name": "Player"}, {"owner_id": "asset.ui", "kind": "asset", "name": "HUD"}],
  "references": [{"from_owner_id": "asset.player", "to_owner_id": "asset.ui", "kind": "references", "evidence": "observed"}]
}
```

## Capture lifecycle and validation boundary

Keep logs/traces/snapshots under an ignored output directory or an explicit user
destination. They are operational captures, not game bundles or source assets.
Collectors must bound queues/storage, record drops, flush and finalize valid JSON
on normal shutdown. Crash-safe chunks/recovery are later exporter work; an
unfinished JSON file is not a valid v1 session. Bound strings/attribute/stack depth
and per-record size at ingestion as well as the number of records.

The four build capabilities remain independent: `RA_ENABLE_LOGGING`,
`RA_ENABLE_PROFILING`, `RA_ENABLE_MEMORY_TRACKING`, `RA_ENABLE_LUA_DEBUGGER`.
Disabled native/.NET instrumentation must avoid argument evaluation; Python/Lua
adapters must report their actual capabilities without claiming native compile-out.
Implementation and ON/OFF/overhead tests belong to subsequent tasks.

For T01 review, validate all schemas against the 2020-12 metaschema and these three
examples with an offline reference registry. Negative cases should reject unknown
versions/fields, missing context, negative time/bytes, scopes without duration,
async/flow events without IDs, nonnumeric counters, complete coverage with drops,
and unavailable metrics represented as zero. Semantic integrity checks and actual
viewer import tests remain mandatory when implementing producers/consumers.
