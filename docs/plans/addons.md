# Raiders Engine addons

Status: Planned. No addon discovery or loading is implemented by Y04.

## Scope and independent execution

An **addon** is a versioned extension installed alongside the engine or its tools.
It can register capabilities without editing core sources: engine services/events,
native types, Lua modules, desktop menus/tabs, setup profiles/recipes and pipeline
stages. NuGet, wheels and native libraries remain implementation/distribution
mechanisms, not alternative user-facing names for addons.

This plan owns U01-U14; the [macro roadmap](../plano-de-desenvolvimento.md) owns
coordination. Each task is a separate PR targeting 3-8 changed paths, at most about
10 including tests, project registration and documentation. Paths below identify
the owning project; reserve integration paths within that budget and split first
if additional files are required. No task implements every host at once.

## Shared contract

- Versioned manifests declare identity, addon/API versions, supported host and
  target, required/optional dependencies, capabilities, entry points and payload
  hashes. Resolve deterministically; reject cycles, collisions and incompatible
  APIs with actionable diagnostics. Lock the selected graph per workspace/build.
- Each host exposes a narrow registration API and explicit startup/shutdown
  ownership. Track registrations, callbacks, jobs, resources and memory by addon.
  Thread affinity, cancellation and shutdown ordering are part of the contract.
  Hot unloading is not assumed safe; specify restart-required behavior first.
- Evaluate a versioned C ABI for native interoperability; do not assume a stable
  C++/STL ABI across compilers. C# hosts use a managed contract, Python hosts use
  installed modules and Lua uses registered modules. Share manifest semantics
  without duplicating the native renderer, scheduler or Lua VM.
- Installed executable addons are trusted code, not a security sandbox. Validate
  origin/integrity and show permission/installation actions before executing them;
  do not silently load arbitrary directories or download SDKs during discovery.
- Authoring reads source XML/Lua directly. Addon types/validators/cookers must
  agree with source and packaged providers. Binary-only designers can use shipped
  addons; adding native code still requires a developer-built compatible artifact.
- Distinguish Windows host support from build targets. Future Android/console
  providers should supply SDK recipes, input/platform events and packaging hooks
  through contracts. This plan adds the seams and a fake provider only; it does
  not add another supported platform or distribute restricted SDKs.
- All hosts expose structured logs, optional profiling and memory attribution for
  addon discovery/loading/callbacks. Faults identify the addon and preserve the
  last-good configuration; process isolation is a separately evaluated policy.

## Implementation backlog

**U01 - Addon manifest and host extension ADR.** Depends: Q02, T01.
Deliverable: `docs/addons.md`, `schemas/addon.schema.json` and owned contract
fixtures/tests (3-6 paths). Define lifecycle, trust, host/target capability IDs,
API compatibility, registration ownership and restart policy. Compare native and
managed loading boundaries before selecting them; no loader implementation.

**U02 - Addon dependency resolver and lock contract.** Depends: U01.
Deliverable: `tools/dotnet/Raiders.Addons.Contracts/` resolver/DTO library and
MSTest project (5-8 paths). Accept deterministic ordering, cycle/version/conflict
rejection, optional dependency behavior, disabled addons and stable lock output.
Publish the resolver as a NuGet library; native/Python consumers use the same
resolved manifest/lock format rather than depending on the .NET runtime.

**U03 - Native addon loading and lifecycle.** Depends: U01, B07, T03.
Deliverable: `engine/addons/` loader, versioned API header, GoogleTest fixtures and
CMake registration (5-8 paths). Verify missing entry points/API rejection,
registration cleanup and restart-safe shutdown; no arbitrary hot unload.

**U04 - Native service and event extension points.** Depends: U03, C04, B02, V09.
Deliverable: native platform/input/service registration and tests in
`engine/addons/` (4-7 paths). A sample addon observes events and registers a service
without changing the host; jobs/callbacks drain on the correct owning threads.

**U05 - Managed addon host library.** Depends: U02, Q05, T05.
Deliverable: `tools/dotnet/Raiders.Addons.Host/` and MSTest project (5-8 paths).
Load compatible trusted addons, resolve contract identity and native payload
locations, surface failures and clean up subscriptions. NuGet transport must not
turn every installed package into an automatically executable addon.

**U06 - Shared desktop menu and tab contributions.** Depends: U05, J02.
Deliverable: `Raiders.Tools.Common` contribution registry and an example addon
with MSTest coverage (4-8 paths). UI/entity/level editors and inspectors consume
the common host. Reject duplicate command IDs; UI work runs on the UI thread;
disabled/failed extensions leave core documents and native preview usable.

**U07 - Environment setup addon contributions.** Depends: U05, Y06.
Deliverable: environment profile/probe/recipe contribution adapter and tests
under `tools/dotnet/Raiders.Environment.Core/` (4-7 paths). GUI and doctor share
the resolved catalog, fingerprints and action review; invalid/conflicting
contributions cannot replace approved core recipes silently.

**U08 - Python pipeline addon stages.** Depends: U02, Q04, T06.
Deliverable: installed `raiders_pipeline` addon adapter, pytest cases and versioned
stage contract (4-7 paths). Resolve pinned wheel entry points and validate stage
inputs/outputs, ordering, cancellation and failure without editing the coordinator.
Native stages use a versioned executable/library adapter, not a second scheduler.

**U09 - Addon Lua modules and API definitions.** Depends: U04, G11.
Deliverable: native Lua registration adapter and scripting tests (4-7 paths).
Keep addon module namespaces, generated LuaLS definitions and callback ownership
consistent; incompatible modules fail with addon identity and source diagnostics.

**U10 - Addon asset types and cooking registration.** Depends: U04, U08, I09, R09.
Deliverable: type/validator/cooker contribution contract and integration fixtures
(4-8 paths). New addon data loads through source providers without cooking;
explicit packaging resolves required addon types and binary compatibility.
Missing addons and unsupported schema versions fail before mutating live content.

**U11 - Addon artifact installation and distribution.** Depends: U02, U05, Y05.
Deliverable: addon artifact/lock acquisition, repair adapter and tests (4-8 paths).
Verify payloads, stage updates atomically, preserve last-good state and support
offline approved artifacts. Keep addon installation distinct from SDK preparation.

**U12 - Platform provider extension acceptance.** Depends: U04, U07, U08.
Deliverable: fake target addon and contract tests (4-7 paths). Demonstrate SDK
recipe discovery, input/system hooks and packaging stage selection without core
source changes. Windows remains the only validated real host/target; unavailable
external SDKs report unavailable and are never committed or implicitly acquired.

**U13 - Addon diagnostics and contribution inspection.** Depends: U06, U07, U08, U09, U10, U11.
Deliverable: shared C# addon inspection view and diagnostic integration (4-8 paths).
Show resolved versions, capabilities, failures, trace identity and resource costs;
distinguish measured memory from estimates. Failed addons remain identifiable.

**U14 - Cross-host addon compatibility acceptance.** Depends: U12, U13.
Deliverable: cross-project integration fixtures and selective CI rules (4-8 paths).
Exercise engine, desktop, setup and pipeline addons through compatible/mismatched
versions, disabled/missing dependencies, cancellation and restart. Verify source
versus cooked content parity and binary-only consumption of approved artifacts.
