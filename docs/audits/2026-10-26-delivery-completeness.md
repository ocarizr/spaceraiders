# Delivery completeness audit — 2026-10-26

Baseline: master `2832f5b`, Y06 merged in PR #26. Scope: all 22 task IDs then
marked completed (A01-A05, Q01/Q02/Q05-Q07, T01-T06 and Y01-Y06), their code,
owned tests, CI and acceptance wording. This is a product-level review requested
by the maintainer, not an agent execution log. No implementation gap is repaired
by publishing this document. No real SDK installer ran during the audit.
Concurrent uncommitted presentation/package work was identified and explicitly
excluded by the maintainer from this audit's staging. The findings describe the
baseline commit, not an acceptance judgment on that in-progress implementation.

## Conclusion

The repository contains working native/managed/Python foundations, not a complete
environment setup product. Fixture tests were repeatedly treated as stronger
evidence than they provide, and dependency/status reporting failed to expose
missing production integrations. Some limits were documented in detailed files,
but they were not consistently carried into task acceptance, the diagram and
delivery summaries. The earlier Y07 acceptance was also reduced to available
components; that concealed the unfinished native/.NET/Python development workflow.

The largest implementation defect found is destructive venv replacement without
last-good activation. Y05's original acceptance is therefore **reopened**, despite
its merged PR. Keep that history; do not call the acceptance complete or quietly
rewrite it as journaling only. Other completed tasks remain bounded contract/library
deliveries, with the missing integrations explicitly assigned below.

Passing tests are useful evidence. They are not evidence that every branch, real
SDK installation, supported language or downstream product works. This audit is
not an exhaustive proof that no additional defects exist.

## Evidence vocabulary

| Level | What it proves | What it does not prove |
| --- | --- | --- |
| Contract/schema | Defined/validated data shape and invariants | A usable producer, adapter or application |
| Unit/fixture integration | Algorithms, edge cases and composition under controlled inputs | Real SDK metadata, installer semantics or production bindings |
| Real local invocation | Actual compiler/package/process/file behavior on the inspected machine | Clean-machine provisioning, all supported configurations or all failure paths |
| Product acceptance | A named real consumer completes the declared workflow with reviewed artifacts | Unlisted platforms, future integrations or unrelated capabilities |

Fakes remain appropriate for fault injection, cancellation and uncommon branches.
They must be labeled. A screenshot using a fake backend proves rendering/binding
of that fixture, not installation support. A real file containing synthetic SDK
bytes is a filesystem integration test, not validation of a Windows SDK.

## Task-by-task disposition

| Task | Delivered and evidence | Remaining boundary / disposition |
| --- | --- | --- |
| A01 | Source/PDF reference, original build failure and diagnostic baseline recorded in [legacy-game](../legacy-game.md) | Full manual playthroughs were explicitly unverified. Documentation accepted; no claim that gameplay is correct or the target sample exists. A09-A11/N tasks retain gameplay work. |
| A02 | Generated IDE/query files removed; ignore policy | Source-control hygiene, not a runtime feature. No missing production adapter identified. |
| A03 | Tracked CMake cache removed | Source-control hygiene; local build caches still need reconfiguration and are not portable acceptance artifacts. |
| A04 | Windows C++20 target/presets and real native compilation | Effective native pins are not fully checked by builds/CI: Q12. Not an extracted engine library. |
| A05 | GoogleTest/CTest coordinate cases, native build and required CI gate | Real tests, but a small vector slice, not gameplay/physics/renderer coverage. A06-A11 and O01-O08 are still future work. |
| Q01 | CMake/.NET/Python versus Bazel ADR and bounded spikes | A decision, not a monorepo coordinator, setup system or all-language implementation. Q03/Q04 remain future. |
| Q02 | SDK/package/Python/native policy files | .NET policy is active; native image/tool versions are not fully enforced. Q12 verifies effective inputs. Pins alone do not acquire SDKs. |
| Q05 | Real MSBuild pack/restore on generated Foundation/Feature/Consumer fixture projects; immutable collision checks | Production core/setup still use ProjectReference. Q08-Q10 supply real package version/consumption acceptance. |
| Q06 | Installed pipeline wheel/module/console entry, real pip build/install on a fixture producer | Valid reusable CLI slice, not all production build stages. Q11 adds actual repository producer and atomic immutable publication. |
| Q07 | Real isolated build/tools venvs, locked wheels, local pipeline wheel, drift/recreation tests | Destructive recreation, no last-good transaction. Interpreter installer metadata exists but native installer acceptance is absent. Y15/Y16 and Y19 close these gaps. |
| T01 | Log/trace/memory schemas and semantic terminology | Memory/trace schemas are not collectors. T09-T12 remain unimplemented. |
| T02 | Real spdlog/file/thread logging library with bounds, filters and shutdown tests | Legacy game does not consume it. T07 is the planned native application integration; no high-performance benchmark claimed. |
| T03 | Native ON/OFF scope capture, nested/concurrent/bounded tests | Collector library, not a capture from real gameplay. T07/T14 retain integration/overhead acceptance. |
| T04 | Real native JSON writer, interrupted child-process recovery and historical pinned Perfetto SQL import documented in [observability](../observability.md) | Current CI does not reproduce the viewer import. T19 makes that reusable validation; no new viewer import was performed in this audit. |
| T05 | Managed logging/context/bounded scope library; real tests | Scope disposal rejects thread switches; no async-operation span abstraction or application export. T16/T17. |
| T06 | Installed Python telemetry library and actual child-process wrapper tests | Wheel/venv/code-style production stages do not import the adapter. T18 supplies current pipeline integration; memory/export is not implemented by this task. |
| Y01 | Profile/health contracts and schema fixtures | Lists five profiles, not five working integrations. Production capability evidence is separate. |
| Y02 | Real executable discovery APIs, injectable protocols, hashed manifest verification and synthetic-file tests | Native SDK/Lua/package manifest tests use synthetic artifacts. No production manifests/bindings exist for most profiles. Y08-Y13/Y19. |
| Y03 | Prerequisite recipe runner, verified acquisition, process adapter and fake-installer tests | Generic engine, not a complete recipe catalog. Real third-party installer return/repair behavior remains Y19. |
| Y04 | Actual self-contained WinForms EXE and published startup test; UI flow with fake backend | Only CPython installation is bound. UI text/theme/policy are embedded in app classes. Y08-Y13, J09-J13/Y18. |
| Y05 | Durable journal, replay/locking/cancel/pending-action tests; no-op healthy prerequisites | **Acceptance reopened:** no transactional replacement preserving the previous usable venv. Y15/Y16 precede Y05 revalidation. |
| Y06 | Actual published headless processes, JSON/schema/exit codes, consent/deadline/redaction tests | Reuses Y04's partial backend; it adds no missing SDK recipes. Profiles listing is metadata, not availability. Async profile/export gaps remain T16/T17. |

## Current production setup matrix

Source: `WindowsSetupBackend` in
[ProfileViewModel.cs](../../tools/dotnet/Raiders.Environment.Setup/ProfileViewModel.cs).

| Profile | Real production binding today | Missing real inputs/integration | Owner |
| --- | --- | --- | --- |
| Native development | CMake/Ninja executable protocols, only bound when found on PATH | VS/MSVC/toolset/Windows SDK discovery, approved acquisition payloads and bundled build-tool paths | Y08/Y09, Q12, Y19 |
| .NET tooling | Actual dotnet SDK probe | SDK acquisition and NuGet config/feed/lock/package closure; real packaged application consumers | Y10, Q08-Q10, Y19 |
| Python pipeline | Actual interpreter probe and approved CPython download/install recipe | GUI/headless build/tools venv adapters; authoritative release wheel identity; transactional activation and real installer acceptance | Y11, Y15/Y16, Y19 |
| Lua development | Catalog entries; generic library protocol/manifest APIs | Approved runtime/compiler/debugger/editor payloads and production registrations | Existing G tasks, Y12/Y14 |
| Designer tools | Catalog entries | Actual SDK distribution manifest/payload and optional Lua editor binding | P07/X04, Y13/Y14 |

Only one recipe is registered in production: CPython. Unbound requirements return
Unknown/RecipeUnavailable; they are not secretly implemented by fake installers
in shipping code. The misleading part was presenting the surrounding task/product
as broadly finished. Neither the GUI gallery nor 119 core tests changes this matrix.

A fresh real doctor invocation in this audit selected the first three profiles:
native was Unknown for all five requirements; .NET SDK 10.0.402 was Ready while
managed.packages was Unknown; Python 3.14.3 was detected but incompatible with the
approved 3.12 versions, while python.build/tools were Unknown. This is one machine's
observation, not portable defaults. The supported native tools were usable through
Visual Studio's developer environment, demonstrating the missing discovery coverage.

## Concrete findings and closure

| Finding | Code/evidence and practical impact | Closure |
| --- | --- | --- |
| Missing production adapters | The backend registers four executable probes and one CPython recipe. No full native/.NET/Python profile can be provisioned by the current GUI/headless host. | Y08-Y13 already cover the bindings; Y19 adds real current-profile acceptance. Y07 now depends on it. |
| Last-good replacement absent | [`create_env._create_environment`](../../tools/pipeline/wheel_build/src/raiders_pipeline/create_env.py) calls shutil.rmtree(target) before venv creation/pip installation. Input hash validation does not protect against a later install failure. Journaling in RepairPlan cannot restore deleted files. | Y15 contract, Y16 implementation; reopen Y05. |
| Production package path unproven | Real csproj files use ProjectReference despite Q05 fixture PackageReference tests. The setup can succeed from sources while a NuGet consumer fails. | Q09/Q10. |
| Immutable identity reused | Environment.Core remains version 0.2.0 from Y03 through Y05/Y06 changes. The immutable publisher correctly rejects differing same-version payloads; fresh CI feeds do not expose an upgrade collision. | Q08 before product package adoption. |
| Wheel publication is not atomic/immutable | [`build_wheel`](../../tools/pipeline/wheel_build/src/raiders_pipeline/build_wheels.py) publishes with shutil.copyfile(source, artifact), overwriting an existing name. Q06 fixture success does not validate failed-publication preservation. | Q11, with a real repository producer. |
| Profiling is not yet usable from current tools | [`ProfileScope`](../../tools/dotnet/Raiders.Diagnostics/ProfileScope.cs) rejects different-thread disposal. HeadlessCommands scopes span awaits, so a continuation thread change can reject a timing event. Setup does not drain/export TelemetrySession; Python's adapter is not wired into current wheel/venv commands. | T16/T17/T18. Existing T13/T15 were too late to close today's tool path. |
| Policy is broader than enforcement | C++ CI selects the installed VS compiler/SDK; exact native policy versions are not checked. The build works but is not proof of the pinned environment. | Q12 and Y08/Y09. |
| Catalog/journal grammar differs | EnvironmentProbe accepts colon/slash in IDs; RepairPlan.ValidId excludes them. A valid catalog can generate a journal that its next session rejects. | Y17; no claim that arbitrary valid catalogs are proven by the current fixtures. |
| UI/reuse/resource layer absent | [`SetupWindow`](../../tools/dotnet/Raiders.Environment.Setup/SetupWindow.cs) creates raw controls with inline colors, dimensions and English labels. ProfileViewModel also owns policy loading, discovery and recipe composition. No shared UI/resource NuGet library exists. | J09-J16 and Y18; future J02/Z02 explicitly depend on J12. |
| Localization absent from the plan | There is no string catalog/editor/runtime/bundle format, placeholder/fallback policy or text-rendering acceptance. A resource table alone cannot prove Japanese text or layout support. | New [localization plan](../plans/localization.md): F11-F14, H08-H12, K08-K12; shared tool resources J10. |
| Native application integration still future | Root CMake builds raiders_diagnostics but SpaceRaiders does not link/use it. Runtime/preview/game assets, Lua and memory attribution are not delivered. | Existing T07-T15, B/G/R and editor/sample tasks. This is planned work, not hidden test data. |
| Public status/evidence drift | Owning plans still called merged Q06/Q07/T06/Y06 local/pending; red nodes mixed bounded library deliveries with full capabilities. Y07 acceptance had been weakened instead of closing current environments. | Reconcile history, reopen unmet Y05 acceptance, add explicit closure dependencies and require the handoff below. |

## Validation actually performed in this audit

- Native Release rebuild and CTest with logging/profiling ON: 33 cases passed.
  An earlier cached profiling-only configuration also passed 27 cases. Initial
  out/build preset caches had missing/disabled tests; they were not counted as
  passing, and an actual existing build was reconfigured/rebuilt instead.
- Managed Release with profiling ON: diagnostics 13, package graph 8, environment
  core 119, setup/headless 32 cases passed. The latter used the published EXE.
- Installed Python suites: pipeline wheel/venv 26, telemetry 37, contracts 47 passed.
  The first sandboxed pytest attempt could not create temporary directories;
  rerunning with the required filesystem access passed. That was not classified
  as an application defect. Tests create real isolated venvs and install real wheels;
  SDK/manifest/installer fakes are identified separately above.
- Real published doctor inspected native/.NET/Python, without preparing/installing
  anything. Package-version comparison and code/CI searches confirmed the findings.
- Diagram validation checks every task/edge, duplicate IDs, cycles, completion
  prerequisites, readiness, colors, summary counts and local document links.

This run did not install/uninstall SDKs, perform manual full-game playthroughs,
re-import Perfetto traces, benchmark performance or prove all OS/installer failure
paths. Those results must not be inferred from the passing test counts. Generated
reports and temporary probes remain local; the reusable product findings are here.

## Mandatory delivery and discovery protocol

Before implementation, enumerate the task's branches: supported languages/roles,
real producers/consumers, states, failure/cancel/restart/upgrade paths and data sources.
Classify each as implemented, deferred with a task/dependency, or out of scope with
a stated reason. Missing runtime data/configuration is unfinished product work
unless the task is explicitly a contract-only delivery.

The roadmap adds 36 separately scoped closure/presentation/localization tasks,
bringing the total to 292. The diagram retains 22 merged PR records, counts 21
accepted completions after reopening Y05, and recomputes the ready frontier from
all 697 prerequisite edges. No new implementation task is marked complete here.

Use SOLID and composition at actual variation boundaries: UI, application workflow,
discovery, acquisition, recipes, package transport and content providers. Keep shared
code in owned packages, inject external effects, avoid duplicating state machines
and add design patterns only when they solve a named problem. Tests must cover
both isolated behavior and the declared real consumer; do not create fake product
claims merely to satisfy an interface or a screenshot.

When a gap is discovered, record impact immediately, update the owning backlog and
dependency diagram, and tell the maintainer during the activity. Do not wait for
them to discover it. Reopen unmet acceptance rather than retroactively narrowing
it. A changed plan is a reviewed product deliverable, not a fixed script to obey
after its assumptions have failed.

Every final handoff must state: (1) what works in the real product, (2) what used
fixtures and what real data was exercised, (3) missing adapters/data/branches and
their task IDs, (4) acceptance/completion status and plan changes, (5) validation
results and material limits, plus staging/commit guidance. A generic "all tests
passed" or "task complete" is insufficient when production branches are deferred.
If there are no known gaps in the declared task scope, say that explicitly without
claiming exhaustive coverage of all future product behavior.
