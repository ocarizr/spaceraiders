# C# level editor and integrated playtest

Status: Planned. Implementation has not started.

## Approved Play integration

G23/G25 gate W03 acceptance: fresh two-VM sessions share one serial native script
lane; source assets are snapshotted for Play, not automatically cooked/reloaded.
Pause freezes simulation debt/timers; single-step completes one fixed tick and its
contacts, followed by UI/render refresh. Stop settles native work and cleanup before
freeing VM/World memory. See [runtime design](../runtime-design.md).

## Scope and independent execution

A deliberately small C# scene/level authoring tool: entity instances, transforms,
layers, spawn settings, UI references and play/pause/step/stop.
Inputs: shared C# platform, bundle scenes, authored entity assets, native renderer,
LuaJIT debugging and runtime snapshots. Exports: scene bundles and reproducible
playtest sessions. First slice: place two entity assets, press Play and run the real
engine in the native preview host without cooking, packaging or compiling application code.
No 3D, terrain system, shader editor, lighting pipeline or general-purpose IDE.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Implementation backlog




## Product boundary

This is a scene composer/playtest tool, not a full world/terrain editor. Create
levels from available entity definitions, place/rotate instances, choose render
layers, reference UI, set spawn points/waves and select the Lua scene behavior.
A blank project plus templates must be sufficient for a designer to build a small
scene without programming in C++, C# or Python.

Playtest runs a separate native World/VM in PreviewHost using the same renderer.
Play/pause/single-tick/stop and debugger pauses have one coordinated state machine.
Stopping discards simulation mutations; copying playtest changes back requires an
explicit future feature and is outside the first delivery. Authored bundles remain
editable while running a last-good preview; refresh/restart is explicit.

**W01 - C# scene application and documents.** Depends: J05, J06, J08, F08.
Paths (6): `tools/dotnet/Raiders.LevelEditor/{Raiders.LevelEditor.csproj,Program.cs,
SceneDocument.cs,SceneTree.cs}`, `Raiders.Tools.sln`,
`tests/dotnet/Raiders.Tools.Tests/SceneDocumentTests.cs`.
Acceptance: create/open/save scene member in a source bundle; no other frontend
build is required; invalid scenes preserve the current document.

**W02 - Place entities, transforms and layers.** Depends: W01, L01, D09.
Paths (5): `tools/dotnet/Raiders.LevelEditor/{SceneCanvas.cs,SceneCommands.cs,
InstanceInspector.cs,SceneDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/SceneEditTests.cs`.
Acceptance: pick bundle asset, place/rotate/duplicate/delete, set layer and override
supported fields with undo/redo; new IDs/references survive save/reopen.

**W03 - Native play/pause/step/stop without packaging.** Depends: W02, X02, J08, G15, G23, G25.
Paths (5): `tools/dotnet/Raiders.LevelEditor/{PlaytestController.cs,
PlaytestToolbar.cs,SceneDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/PlaytestTests.cs`, `docs/level-editor.md`.
Acceptance: deserialize original source-bundle XML/Lua directly into PreviewHost;
no temporary cooked data, offline bytecode or generated C++ is created or required.
Assert zero cooker/codegen/linker calls and Play works without packaged outputs;
the same native renderer draws it;
stop/reset leaves authoring unchanged and Lua breakpoints preserve pause semantics.

**W04 - Spawn configuration and animation preview.** Depends: W03, D08, F10.
Paths (4): `tools/dotnet/Raiders.LevelEditor/{SpawnInspector.cs,
PlaytestController.cs}`, `tests/dotnet/Raiders.Tools.Tests/SpawnPreviewTests.cs`,
`docs/level-editor.md`.
Acceptance: spawn markers/waves, initial animation and scene-script references save
as data; preview shows transitions/contact activation at the right tick.

**W05 - Playtest logs/profiling/memory integration.** Depends: W03, T15, S08.
Paths (4): `tools/dotnet/Raiders.LevelEditor/{PlaytestDiagnostics.cs,
PlaytestController.cs}`, `tests/dotnet/Raiders.Tools.Tests/PlaytestDiagnosticsTests.cs`,
`docs/level-editor.md`.
Acceptance: capture frame/physics/script timings and selected-entity memory; open
the corresponding trace/snapshot or Lua source without attaching to the wrong host.

**W06 - Designer end-to-end acceptance.** Depends: W04, W05, X06, P07.
Paths (4): `tests/integration/designer_playtest_tests.py`,
`tests/dotnet/Raiders.Tools.Tests/LevelWorkflowTests.cs`,
`docs/level-editor.md`, `docs/designer-quickstart.md`.
Acceptance: user with only shipped binaries and optional VS Code creates an entity,
UI and scene, debugs Lua, playtests and produces a content update loaded by the
existing compatible game; no packaging needed for Play and no compilers/Python SDK.
