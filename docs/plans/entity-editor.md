# C# entity, drawing and animation editor

Status: Planned. Implementation has not started.

## Scope and independent execution

C# entity composition, primitive drawing/sprite authoring, physics inspector,
animation clips and links to Lua behavior. No arbitrary collision mesh authoring.
Inputs: shared C# tooling, bundle/schema services, native renderer and physics.
Exports: entity/sprite/drawing/animation records consumable by the game and level editor.
First slice: author a rotating ship with a primitive or sprite, attach supported
components and save/preview without compiling C++, C# or Python.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Shared job-system handoff

The [jobs plan](jobs.md) owns the scheduler and language wrappers. Added V
prerequisites apply to the integration tasks below; synchronous contracts and
existing completed tasks remain valid. The [task diagram](../task-dependencies.md)
shows cross-project readiness and must be refreshed after dependency changes.

L07 consumes the V17 common job service for authoring/preview background work
and document-lifetime cancellation. Workers publish results to the UI owner and
never replace the native preview renderer.

## Implementation backlog

**L01 - Aplicação C# e composição de entidades.** Depende: J05, J06, I07.
Arquivos (6): `tools/dotnet/Raiders.EntityEditor/{Raiders.EntityEditor.csproj,
Program.cs,EntityEditorDocument.cs,ComponentInspector.cs}`, `Raiders.Tools.sln`,
`tests/dotnet/Raiders.Tools.Tests/EntityDocumentTests.cs`.
Aceite: criar Transform/Sprite e salvar/reabrir XML membro de bundle; registry
fornece defaults/constraints ao editor por schema, sem link da ABI C++.

**L02 - Pintura de sprites em canvas C#.** Depende: L01, F07.
Arquivos (5): `tools/dotnet/Raiders.EntityEditor/{SpriteCanvas.cs,SpriteCommands.cs,
SpritePalette.cs,EntityEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/SpriteEditTests.cs`.
Aceite: pintar/apagar/resize/símbolo/cor/origem com undo; sprite é registro do bundle
e aparece igualmente no preview nativo; falha no save preserva original.

**L03 - Inspector de física, tiro e script.** Depende: L02, G05, M01.
Arquivos (5): `tools/dotnet/Raiders.EntityEditor/{ComponentInspector.cs,
EntityEditorDocument.cs}`, `tools/authoring/src/EntityXml.cpp`,
`tests/dotnet/Raiders.Tools.Tests/ComponentAuthoringTests.cs`, `docs/entity-format.md`.
Aceite: extensões do jogo vêm de metadados/protocolo; Weapon não vira tipo fixo
da engine nem exige linkar o frontend C# à biblioteca nativa do jogo.

**L04 - Preview isolado de entidade.** Depende: L03, J08.
Arquivos (4): `tools/dotnet/Raiders.EntityEditor/{EntityPreviewController.cs,
PreviewInspector.cs,EntityEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/EntityPreviewTests.cs`.
Aceite: run/pause/reset usa física/Lua do host, sem gravar de volta mudanças da
simulação no documento de autoria.

**L05 - Browser de assets e referências.** Depende: L04, K05, R03.
Arquivos (5): `tools/dotnet/Raiders.Tools.Common/{AssetBrowser.cs,AssetReferenceService.cs}`,
`tools/authoring/src/AssetGraph.cpp`,
`tests/dotnet/Raiders.Tools.Tests/AssetReferenceTests.cs`, `docs/editors.md`.
Aceite: duplicar gera IDs; rename preserva referências; remover membro/bundle
usado lista consumidores e permite cancelar; abre seleção no inspetor de grafo.


## Drawing, animation and tooling integration

The authoring canvas may show selection handles, guides and collision outlines.
The content preview itself is the native renderer's output, with the same camera,
layer ordering and animation evaluation as the game. A collision primitive is
chosen independently of the drawn outline; no free-form collider mesh editor.

**L06 - Primitive/pen drawing and image sprite authoring.** Depends: L02, D07, F09.
Paths (5): `tools/dotnet/Raiders.EntityEditor/{DrawingCanvas.cs,DrawingCommands.cs,
SpriteImportService.cs,EntityEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/DrawingEditTests.cs`.
Acceptance: pen/stroke and triangle/rectangle/circle draw tools, image import,
origin and transforms save as bundle records; native preview matches; import is
undoable and invalid images preserve the old asset.

**L07 - Animation clip authoring and timeline preview.** Depends: L06, D08, F10, J08, V17.
Paths (5): `tools/dotnet/Raiders.EntityEditor/{AnimationTimeline.cs,
AnimationCommands.cs,AnimationPreviewController.cs,EntityEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/AnimationEditTests.cs`.
Acceptance: sprite frames, transform keys, duration, looping and event markers;
scrub/reset samples the real native animator; save/reopen and undo preserve IDs.

**L08 - Layer/collider/debug and footprint integration.** Depends: L07, D09, E05, G15, S08.
Paths (5): `tools/dotnet/Raiders.EntityEditor/{ComponentInspector.cs,
EntityPreviewController.cs,EntityEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/EntityWorkflowTests.cs`, `docs/editors.md`.
Acceptance: select layer and simple collider type/rotation, open Lua breakpoint
workflow, capture player footprint and navigate to inspector without losing document.
