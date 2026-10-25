# Windows development environment manager

Status: Y01 contract implemented and locally validated, pending review/merge.
Q07's Python recipes are merged; the C# probe library, setup application and
remaining native acquisition/execution adapters are planned.

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

## Implementation backlog

Each ID is a separate task/PR, normally 3-8 changed paths and at most about 10
including project registration/tests. Q01 only adds this plan. Bootstrap UI
framework selection must not depend on completion of the engine's editor shell.

**Y01 - Setup profiles and health contract.** Depends: Q01, T01.
Status: implemented and locally validated on `Y01_setup_profiles_and_health_contract`,
pending review/merge. See [contract and validation](../environment-setup.md).
Paths: `schemas/environment-profile.schema.json`,
`schemas/environment-health.schema.json`, `docs/environment-setup.md`,
`tests/integration/environment_setup/{fixtures/environment-profile.json,fixtures/environment-health.json,test_contract.py,requirements.lock}`;
CI and plan/diagram updates complete the ten-path delivery.
Acceptance: roles/profiles, Windows architecture, version constraints, probe
evidence and all health states are specified; no game/Python startup dependency.

**Y02 - C# probe library.** Depends: Y01, Q02, Q05, T05.
Paths (6): `tools/dotnet/Raiders.Environment.Core/{Raiders.Environment.Core.csproj,
EnvironmentProbe.cs,ProfileHealth.cs,ToolDiscovery.cs}`,
`tests/dotnet/Raiders.Environment.Tests/{Raiders.Environment.Tests.csproj,ProbeTests.cs}`.
Acceptance: explicit native/.NET 10/Python/Lua/package probes; complete/missing/
wrong-version/stale states tested with fixtures, not real machine modifications.
The reusable library supports versioned NuGet packaging.

**Y03 - Selected-profile recipe execution.** Depends: Y02, Q06, Q07.
Paths (5): `tools/dotnet/Raiders.Environment.Core/{RecipeRunner.cs,
EnvironmentState.cs,ArtifactAcquisition.cs}`,
`tests/dotnet/Raiders.Environment.Tests/RecipeTests.cs`, `docs/environment-setup.md`.
Acceptance: ordered prerequisites, checksummed artifacts, cancellation/progress and
post-step validation; healthy state is a no-op. Tests use isolated fake installers.

**Y04 - Self-contained .NET 10 setup GUI.** Depends: Y03.
Paths (6): `tools/dotnet/Raiders.Environment.Setup/{Raiders.Environment.Setup.csproj,
Program.cs,SetupWindow.cs,ProfileViewModel.cs}`,
`tests/dotnet/Raiders.Environment.Tests/SetupViewModelTests.cs`, `docs/environment-setup.md`.
Acceptance: show health/evidence, select actions and report progress/failures;
launch without .NET/Python SDK; use shared probes. Split framework-generated files
into sub-deliveries if the path budget would be exceeded.

**Y05 - Repair and interrupted setup.** Depends: Y04.
Paths (5): `tools/dotnet/Raiders.Environment.Core/{RepairPlan.cs,
EnvironmentState.cs,RecipeRunner.cs}`,
`tests/dotnet/Raiders.Environment.Tests/RepairTests.cs`, `docs/environment-setup.md`.
Acceptance: diagnose stale package/venv fingerprints and incomplete downloads;
preserve last-good state, retry failed steps and explain offline failures.
Do not remove unrelated user toolchains or project configuration.

**Y06 - Headless setup/doctor and CI.** Depends: Y03.
Paths (5): `tools/dotnet/Raiders.Environment.Setup/{Program.cs,HeadlessCommands.cs}`,
`tests/dotnet/Raiders.Environment.Tests/DoctorTests.cs`,
`.github/workflows/ci.yml`, `docs/environment-setup.md`.
Acceptance: inspect/prepare selected profiles with exit codes and versioned JSON;
share GUI probes/recipes and logs/profiling. Detect drift instead of trusting
a prior Ready marker.

**Y07 - Bootstrap distribution and acceptance.** Depends: Y05, Y06.
Paths (5): `tools/bootstrap/setup.ps1`, `tools/pipeline/package_setup.py`,
`tests/integration/environment_bootstrap_tests.py`,
`docs/environment-setup.md`, `.github/workflows/release.yml`.
Acceptance: a Windows clone obtains/launches a verified setup artifact without
Python/.NET SDK; selected setup reaches Ready, reopens without reinstall and repairs
an isolated incomplete environment. No interpreter/SDK/venv/binary is committed;
distribution/update ownership is explicit.
