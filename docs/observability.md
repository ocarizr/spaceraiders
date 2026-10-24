# Shared telemetry contract (T01)

Version 1 defines data exchanged by native C++20, C#/.NET 10, Python and Lua
adapters. It implements schemas and terminology, not a collector, profiler,
allocator, viewer or logging backend. The native spdlog adapter starts in T02;
clock conversion, bounded collection and viewer interoperability are later work.

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
