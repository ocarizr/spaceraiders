# Raiders Engine launcher

Status: Planned. The Y04 environment setup application is not this launcher.

## Scope and independent execution

A self-contained C#/.NET 10 Windows application is the central entry to installed
Raiders tools, project workspaces, test suites and build/content operations.
It discovers available capabilities and invokes their existing versioned CLI/IPC
contracts. It does not duplicate editors, test runners, setup recipes or cooking.
This plan owns Z01-Z09; see the [macro roadmap](../plano-de-desenvolvimento.md).

Source workspaces expose development actions and environment setup. Binary-only
Designer workspaces expose compatible tools, Lua integration, playtest and content
packaging without requiring compilers/Python. Show why an action is unavailable;
do not launch missing tools or offer full native builds in a Designer-only bundle.
The standalone setup app and headless tooling must still work without the launcher.

Tool/addon manifests declare action IDs, executable/version, arguments, workspace
requirements, output/log protocol and cancellation policy. Resolve paths without
depending on the current directory. Review selected actions, stream bounded logs,
retain exit/results and distinguish success, failure, cancellation and unavailable.
Addon actions follow the common host's lifecycle and trust policy. Test execution
uses GoogleTest/CTest, MSTest/dotnet test and pytest adapters, with Lua's approved
runner when available; the launcher is not another testing framework.

Every task targets 3-8 changed paths, at most about 10 including project/test/build
registration. Split before exceeding the budget. Functional tests mean future
declared suites with result/artifact contracts, not tests invented by the launcher.

## Implementation backlog

**Z01 - Launcher workspace and action contract.** Depends: Q02, Y01.
Deliverable: `docs/launcher.md`, action/workspace schema and contract fixtures
(3-6 paths). Specify source/Designer capabilities, version compatibility,
functional-test descriptors, process ownership and action/result protocols.

**Z02 - Self-contained launcher shell.** Depends: Z01, Q05, T05.
Deliverable: `tools/dotnet/Raiders.Launcher/` app and MSTest project (5-8 paths).
Open without SDK/Python/game/source; select a workspace and show available versus
unavailable actions from installed manifests. No tool executes on startup.

**Z03 - Tool discovery and launch lifecycle.** Depends: Z02, J02.
Deliverable: launcher tool catalog/process adapter and tests (4-7 paths).
Open registered editors/inspectors/code actions; validate executable identity and
compatibility, pass workspace context and handle missing/crashed tools.
Native preview remains owned by its tool; C# never redraws game content.

**Z04 - Test suite execution and results.** Depends: Z02, Y06.
Deliverable: launcher test adapters/results view and isolated tests (4-8 paths).
Select project/suite, run supported framework commands and display results/logs/
artifacts. Include a fake functional-suite descriptor to verify the future path;
real functional suites are added by their owning projects. Cancel/drain children;
unavailable suites do not appear as passing or trigger unrelated suites.

**Z05 - Build validation and packaging actions.** Depends: Z02, Q04, X06.
Deliverable: pipeline action adapter and tests (4-7 paths). Delegate validation,
tools-only builds, content packaging and full-game packaging to existing profiles.
Source editing/playtest never cooks automatically. Designer actions operate with
distributed runtimes; native builds require a developer workspace and toolchain.

**Z06 - Environment setup entry and health summary.** Depends: Z02, Y07.
Deliverable: setup artifact/doctor client and tests (4-7 paths). Source workspaces
can open verified setup and inspect selected health; launch it independently of
the engine/editor. Do not duplicate probe/install logic or mark unknown profiles
Ready. Designer bundles retain their separate distribution/scripting workflow.

**Z07 - Addon management and launcher contributions.** Depends: Z03, U06, U11.
Deliverable: addon list/action adapter and tests (4-7 paths). Show compatible
addons, versions and requirements; delegate install/disable/update to the common
host. Register menu/tab/actions without recompiling the launcher; show restart
requirements, conflicts and failed contributions.

**Z08 - Launcher artifact and workspace distribution.** Depends: Z03, Z06, X03.
Deliverable: launcher publish/distribution manifests and smoke checks (4-7 paths).
Create verified self-contained artifacts for source users and binary-only bundles,
with relocatable tool locations and no committed SDKs, runtimes or generated files.
The Designer SDK can include this artifact without circular packaging dependencies.

**Z09 - Launcher workflow acceptance.** Depends: Z04, Z05, Z07, Z08.
Deliverable: integration fixtures and developer/Designer usage guide (4-8 paths).
From an unrelated directory, exercise tools, tests, cancellation, failure, explicit
packaging, source-only setup and addon actions. Inspect logs/profiling of launcher
and child operations; Designer acceptance requires no development SDK or Python.
