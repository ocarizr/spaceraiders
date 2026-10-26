# Windows development environment manager

Status: Y01-Y05 and Q07 are merged. Y06's headless commands are implemented and
locally validated, pending review/merge. Bootstrap distribution remains Y07;
production profile completion is explicitly tracked in Y08-Y14.

## Scope and independent execution

A graphical C#/.NET 10 tool prepares selected environments after cloning. It shares
a detection/recipe engine with a headless entry point for CI. It is a developer
product, not an agent helper. Policy follows [ADR 0001](../decisions/0001-monorepo-build.md)
and the [build contract](../build-system.md); coordination belongs to the
[macro roadmap](../plano-de-desenvolvimento.md).

Inputs: version/architecture constraints, tool locations, package locks, environment
recipes and approved artifact manifests. Outputs: health reports, prepared local
tools/venvs and repair diagnostics. Workstation preparation must not wait for a
working game, XML editor or Python-based build coordinator.

## User workflow and contracts

1. Open the self-contained Windows setup application. Its own launch requires
   neither an installed .NET SDK nor Python. A bootstrap can retrieve the pinned,
   checksum-verified application artifact; that launcher need not use Python.
2. Scan profiles: Native development, .NET 10 tooling, Python pipeline, Lua
   development and Designer tools. Show versions, paths, architecture and unmet
   requirements, not just whether a directory exists.
3. Report Ready, Missing, Needs repair, Unsupported or Checking/Unknown.
   An incomplete scan is not evidence that a tool is absent.
4. Select actions, show prerequisites, prepare only that closure, show progress
   and recheck health. Ready profiles do not reinstall.
5. Repair failed/outdated recipe steps, preserving source/user settings and
   last-good environments. Partial failures and pending installer/restart actions
   remain visible rather than being labeled successful.

Native profiles detect the Windows C++ toolset/SDK and CMake/Ninja. Managed profiles
validate a .NET 10 SDK and package-feed access. Python profiles discover a suitable
interpreter or obtain a pinned local runtime, then build/install module wheels.
Lua profiles prepare LuaJIT/compiler and describe VS Code extension/debug setup.
Designer profiles use distributed binaries and optional scripting tools, not
developer SDKs by default.

Machine state, interpreter downloads, venvs, package caches and logs stay out of Git.
Recipes/locks describe recreation. An ignored managed cache or distributed artifact
can provide Python; checking a copied interpreter into Git is not the default.
Existing venvs are not copied between machines. Reusable C# core libraries may
become versioned NuGet dependencies of the application.

UI and headless modes share probes, ordering and validation. Provide structured
logs/profiling and machine-readable diagnostics; human logs do not enter a JSON
result stream. Shareable health reports exclude credentials/machine-local secrets.
Setup is independent of gameplay source bundles and never triggers content cooking.

## Current production coverage after Y04

Catalog entries and generic probe protocols are not end-to-end support. The GUI
currently binds .NET SDK, CMake/Ninja and Python executable probes. Its only bound
installation recipe is the approved CPython interpreter bootstrap. MSVC/toolset/
Windows SDK, NuGet closure, Python venvs, Lua and Designer payloads are unresolved
until approved production bindings are supplied. The Q07 venv tools work separately
but are not wired to this GUI. Fixture-based UI screenshots do not prove installation
coverage; see [Y04 capabilities](../environment-setup.md#registered-capabilities-in-this-delivery).

Y08-Y14 close these gaps without rolling every language into one PR. Y07 distributes
the usable approved subset; it does not certify all five profiles. Lua integration
depends on the real VM/debugger contract and Designer setup on the actual SDK
artifact. Do not invent a manifest or checksum to bypass those dependencies.

The [addons plan](addons.md) owns extensible profile/probe/recipe contributions
(U07), including future target SDK providers. Built-in profile completion does not
wait for addons. The [launcher](launcher.md) opens this independent application
and consumes doctor results; neither tool duplicates the other's execution logic.

## Implementation backlog

Each ID is a separate task/PR, normally 3-8 changed paths and at most about 10
including project registration/tests. Q01 only adds this plan. Bootstrap UI
framework selection must not depend on completion of the engine's editor shell.

**Y01 - Setup profiles and health contract.** Depends: Q01, T01.
Status: merged in PR #21. See [contract and validation](../environment-setup.md).
Paths: `schemas/environment-profile.schema.json`,
`schemas/environment-health.schema.json`, `docs/environment-setup.md`,
`tests/integration/environment_setup/{fixtures/environment-profile.json,fixtures/environment-health.json,test_contract.py,requirements.lock}`;
CI and plan/diagram updates complete the ten-path delivery.
Acceptance: roles/profiles, Windows architecture, version constraints, probe
evidence and all health states are specified; no game/Python startup dependency.

**Y02 - C# probe library.** Depends: Y01, Q02, Q05, T05.
Status: merged in PR #22. Implementation and MSTest tests live under
`tools/dotnet/Raiders.Environment.Core/`; DTOs are in `EnvironmentProbe.cs`.
Acceptance: explicit native/.NET 10/Python/Lua/package probes; complete/missing/
wrong-version/stale states tested with fixtures, not real machine modifications.
The reusable library supports versioned NuGet packaging.

**Y03 - Selected-profile recipe execution.** Depends: Y02, Q06, Q07.
Status: merged in PR #23. See [execution and validation](../environment-setup.md#selected-profile-preparation-y03).
Paths (9): `tools/dotnet/Raiders.Environment.Core/{RecipeRunner.cs,
EnvironmentState.cs,ArtifactAcquisition.cs,Raiders.Environment.Core.csproj,
tests/RecipeTests.cs,tests/ArtifactAcquisitionTests.cs}`,
`docs/environment-setup.md`, this plan and `docs/task-dependencies.md`.
Acceptance: ordered prerequisites, checksummed artifacts, cancellation/progress and
post-step validation; healthy state is a no-op. Tests use isolated fake installers.

**Y04 - Self-contained .NET 10 setup GUI.** Depends: Y03.
Status: merged in PR #24. Application and owned MSTest project live under
`tools/dotnet/Raiders.Environment.Setup/`; the entry point shares `SetupWindow.cs`.
Acceptance: show health/evidence, select actions and report progress/failures;
launch without .NET/Python SDK; use shared probes. Split framework-generated files
into sub-deliveries if the path budget would be exceeded.

**Y05 - Repair and interrupted setup.** Depends: Y04.
Status: merged in PR #25. See [durable repair](../environment-setup.md#durable-repair-and-interrupted-setup-y05).
Paths (10): core `RepairPlan.cs`, `EnvironmentState.cs`, `RecipeRunner.cs`,
`ArtifactAcquisition.cs`, owned `tests/RepairTests.cs`; setup `ProfileViewModel.cs`
and `SetupWindow.cs`; `docs/environment-setup.md`, this plan and the task diagram.
Acceptance: diagnose stale package/venv fingerprints and incomplete downloads;
preserve last-good state, retry failed steps and explain offline failures.
Do not remove unrelated user toolchains or project configuration.

**Y06 - Headless setup/doctor and CI.** Depends: Y03.
Status: implemented and locally validated on `Y06_headless_setup_and_doctor`,
pending review/merge. See [headless commands](../environment-setup.md#headless-setup-and-doctor-y06).
Paths (10): setup `SetupWindow.cs`, `HeadlessCommands.cs`, owned `tests/HeadlessTests.cs`;
`schemas/environment-command.schema.json`, `.github/workflows/ci.yml`,
`tests/integration/environment_setup/test_contract.py`, `docs/environment-setup.md`,
this plan, the macro roadmap and the task diagram.
Acceptance: inspect/prepare selected profiles with exit codes and versioned JSON;
share GUI probes/recipes and logs/profiling. Detect drift instead of trusting
a prior Ready marker.

**Y07 - Bootstrap distribution and acceptance.** Depends: Y05, Y06.
Paths (5): `tools/bootstrap/setup.ps1`, `tools/pipeline/package_setup.py`,
`tests/integration/environment_bootstrap_tests.py`,
`docs/environment-setup.md`, `.github/workflows/release.yml`.
Acceptance: a Windows clone obtains/launches a verified setup artifact without
Python/.NET SDK; selected available approved components reach Ready, reopen without
reinstall and repair an isolated incomplete environment. No interpreter/SDK/venv/binary is committed;
distribution/update ownership is explicit. Report unavailable profiles honestly;
complete five-profile acceptance belongs to Y14.

## Production profile completion

Every task includes GUI/doctor registration and meaningful tests within its
3-8 path target (about 10 maximum). Reuse Y02/Y03 logic, Q05 NuGet and Q07 Python
recipes; do not create a second setup engine or install real SDKs in unit tests.

**Y08 - Native compiler and SDK production discovery.** Depends: Y04, Q02.
Deliverable: native discovery adapter, composition bindings, tests and policy docs
under `tools/dotnet/Raiders.Environment.Core/` and `Raiders.Environment.Setup/`
(4-7 paths). Discover approved Visual Studio Build Tools/compiler/toolset/Windows
SDK and bundled CMake/Ninja, including installations absent from PATH. Check actual
versions/architecture and distinguish conclusive absence from failed discovery.

**Y09 - Approved native toolchain acquisition.** Depends: Y08, Y05.
Deliverable: reviewed native installer/component manifest, recipe adapter and
tests (4-8 paths). Show component/version/destination and privilege/restart actions;
acquire only selected prerequisites, validate them after execution, preserve
unrelated installations and support documented approved offline artifacts.

**Y10 - Managed SDK and NuGet closure preparation.** Depends: Y04, Q05.
Deliverable: managed SDK acquisition/restore bindings and tests (4-8 paths).
Use the pinned SDK policy and locked package closure; verify feed/configuration
and actual packages, acquire approved SDK artifacts and restore only selected
projects. Hide credentials in reports; SDK presence alone cannot mean Ready.

**Y11 - Python build and tools venv integration.** Depends: Y04, Q07.
Deliverable: Q07 recipe adapter, health fingerprints and tests (4-7 paths).
Wire the existing interpreter/bootstrap and locked build/tools venv commands into
GUI/doctor with cancellation and postvalidation. Verify installed module entry
points and wheel hashes. No approved pipeline wheel means unavailable, not a
fabricated self-hash or successful tools environment.

**Y12 - LuaJIT and VS Code environment integration.** Depends: Y04, G15.
Deliverable: approved LuaJIT/compiler/editor-extension manifests, probes/recipes
and tests (4-8 paths). Match the actual runtime/compiler and LuaLS/debug adapter
versions; prepare completion/lint/format/debug configuration and verify embedded
breakpoints/locals using G15's workflow. Respect existing user editor settings.
The standalone setup executable must still launch without Lua or VS Code.

**Y13 - Designer binary toolset preparation.** Depends: Y04, P07, X04.
Deliverable: Designer SDK manifest binding, acquisition/health adapter and tests
(4-7 paths). Validate compatible shipped tools/runtime/templates, relocate cleanly
and expose optional approved Lua authoring setup without developer toolchains.
Reject incompatible bundles; never require C++/.NET SDKs or installed Python.

**Y14 - Complete environment coverage acceptance.** Depends: Y07, Y09, Y10, Y11, Y12, Y13.
Deliverable: isolated cross-profile acceptance fixtures, clean-machine procedure
and CI artifact/result integration (4-8 paths). Each advertised supported profile
must reach Ready using production bindings, perform a representative build/run/
debug action, reopen without reinstall and report partial/offline/cancelled states
truthfully. Unit-test fakes and screenshots are not production coverage evidence.
