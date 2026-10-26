# Native audio scope and ownership

Status: Planned requirements work only. No audio runtime, backend, mixer editor or
Lua audio VM is implemented or implied. See the [runtime design](../runtime-design.md).

Audio remains native. A future C# authoring tool may save XML and explicitly cook
binary data through the existing asset model; there is no audio scripting VM.
Device/audio-callback completion is different from CPU job completion. Real-time
callback constraints must not be inherited from a general-purpose worker queue.
This plan currently owns only the decision below; functional implementation tasks
must be proposed with measured acceptance after the scope review, not hidden in jobs.

**AU01 - Audio scope and native ownership decision.** Depends: V01, T01.
Paths (3-5): `docs/decisions/0003-audio-scope.md`, this plan, macro roadmap and task
diagram; use another unused ADR number if needed. Acceptance: review whether the
sample needs audio now, Windows backend candidates, owner/callback constraints,
job bridges, asset/mixer authoring and cooking, cancellation, memory/profiling and
real-data acceptance. Allocate every approved runtime/editor integration as small
dependent tasks before claiming audio support. No implementation is authorized by
this planning task and no unsupported platform delivery is added.
