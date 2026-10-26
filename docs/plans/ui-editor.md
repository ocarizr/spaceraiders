# C# UI editor and shared desktop tooling platform

Status: Planned. Implementation has not started.

The shared visual foundation is J09-J16 below, also used by Environment.Setup and
the launcher. It is a versioned NuGet UI library, separate from asset documents,
native rendering and application behavior. See the [delivery audit](../audits/2026-10-26-delivery-completeness.md).

## Scope and independent execution

C#/.NET 10 desktop UI editor plus shared documents, undo/redo, bundle/protocol clients,
preview client and navigation services used by all developer tools.
Inputs: native preview surface, UI schema/IR, bundle transactions and Lua environment.
Exports: Raiders.Tools.Common/Contracts and Raiders.UIEditor.
The [addons plan](addons.md) owns managed addon loading (U05) and shared menu/tab
contributions (U06) for UI/entity/level editors and inspectors. Reuse one host;
do not create an independent extension loader per editor. The [launcher](launcher.md)
discovers these applications through Z03, without becoming their document shell.
First slice: create a button, save XML/Lua members, preview through the native game
renderer and open its Lua handler at the correct source location in VS Code.
Editing deserializes source XML and loads source Lua directly through the native
development provider, without cooking or generated C++ outputs. The frontend may
draw authoring overlays, but never redraw the scene/UI itself.

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

K05 consumes the V17 common job service for background validation/preview work
and owner-thread result publication. Widget mutation stays on the UI thread;
preview rasterization remains native.

## Shared visual and resource foundation

Interactive tools must consume the same resource/token/control packages. Do not
copy colors, spacing, button implementations or human-facing text into each app.
Application presenters/view models own commands and event behavior; reusable UI
components expose typed properties/events and remain independent of setup/game
services. Stable machine protocol IDs remain invariant constants, not translations.
The framework/resource ADR must address the existing standalone WinForms setup;
do not defer it until asset/editor infrastructure or silently rewrite it in a
second framework. Design for accessibility, DPI, keyboard focus and text expansion.

**J09 - Shared desktop design and framework contract.** Depends: Q02, Y04.
Deliverable: design tokens, component inventory and Windows desktop/resource ADR
(3-5 paths). Define one visual identity, framework interoperability/migration,
dependency boundaries and ownership; the setup UI is the first real consumer.

**J10 - Localizable .NET resource service.** Depends: J09, F11.
Deliverable: `tools/dotnet/Raiders.Tools.Resources/` library, English resource
tables and MSTest project (5-8 paths). Use stable resource keys, fallback, typed
formatting and missing-key diagnostics. Test another locale/long-text fixture;
do not count fixture translations as a supported product language.

**J11 - Styled reusable desktop controls.** Depends: J10, Q05.
Deliverable: `tools/dotnet/Raiders.Tools.UI/` controls/tokens and MSTest project
(5-8 paths). Provide shared buttons, labels, inputs and state styling through
composition; separate commands/events from presentation and test DPI/focus.

**J12 - Published shared UI package contract.** Depends: J11, Q08.
Deliverable: package metadata, real packaged control consumer and locked restore
tests (4-8 paths). Consume UI/resource NuGet artifacts from an unrelated directory;
no source references or copied DLLs may substitute for this acceptance.

**J13 - Migrate setup to shared UI and resource packages.** Depends: J12, Y18, Y06.
Deliverable: setup package references, view/presenter adaptation and regression
tests (5-8 paths). Preserve headless/self-contained behavior, installation review
and cancellation. Remove duplicated style values and human-facing literals from
the view; protocol codes remain stable. Verify the actual published setup window.

**J14 - Shared editor shell presentation.** Depends: J02, J12.
Deliverable: common menus, tabs, dialogs and status/error presentation with tests
(4-8 paths). UI/entity/level editors, inspectors and localization editor inherit
the same presentation services while retaining their own commands/documents.

**J15 - UI resource and design conformance checks.** Depends: J13, J14.
Deliverable: focused analyzers/validators, visual/resource fixtures and CI checks
(4-8 paths). Detect raw user-facing control text, duplicated design tokens and
missing resource keys; explicitly allow invariant protocol identifiers and
reviewed framework constants. Test pseudo-localized expansion and accessibility.

**J16 - Shipped tools visual integration acceptance.** Depends: J15, Z02, U06, K08, L01, S01, W01.
Deliverable: application-manifest driven acceptance cases and product evidence
(4-8 paths). Inspect actual setup/launcher/editor/inspector/localization/addon
views for common controls, resources, DPI and keyboard behavior. A gallery-only
fixture cannot complete this task; split acceptance by app if the budget grows.

## Implementation backlog

**J01 - Definir UX desktop e fronteiras C#/C++.** Depende: F05, Q01, R01, J09.
Arquivos (3): `docs/editors.md`, `docs/entity-format.md`, `docs/tooling-protocol.md`.
Entrega: escolher framework desktop .NET para Windows; canvas, árvore, inspector,
documentos e protocolo de preview. Console continua backend do jogo.
Aceite: desenhar nave, montar UI e inspecionar dependências são fluxos definidos;
a ferramenta C# tem entry point/event loop próprios, sem herdar Application C++.

**J02 - Shell C# e documentos de bundle.** Depende: J01, Q02, R02, J12.
Arquivos (7): `tools/dotnet/Raiders.Tools.Common/{Raiders.Tools.Common.csproj,
EditorDocument.cs,DocumentService.cs,ToolShell.cs}`,
`tests/dotnet/Raiders.Tools.Tests/{Raiders.Tools.Tests.csproj,DocumentTests.cs}`,
`Raiders.Tools.sln`.
Entrega: abrir/criar documento e dirty state. Aceite: bundle inválido preserva
documento anterior; fechar permite salvar/descartar/cancelar; engine não precisa
de .NET para compilar. Arquivos extras do framework entram em sub-PR se necessário.

**J03 - Undo/redo em C#.** Depende: J02.
Arquivos (4): `tools/dotnet/Raiders.Tools.Common/{CommandHistory.cs,
EditorCommand.cs,EditorDocument.cs}`, `tests/dotnet/Raiders.Tools.Tests/UndoTests.cs`.
Aceite: drag é operação única, IDs são preservados e dirty state volta ao ponto salvo.

**J04 - Seleção e inspector compartilhados em C#.** Depende: J03.
Arquivos (5): `tools/dotnet/Raiders.Tools.Common/{SelectionService.cs,
PropertyInspector.cs,CanvasTransform.cs,ComponentSchemaClient.cs}`,
`tests/dotnet/Raiders.Tools.Tests/SelectionTests.cs`.
Aceite: inspector consome schema do pipeline, valida tipos/faixas e converte
coordenadas sem depender da implementação Win32 do jogo.

**J05 - Save e recuperação de bundle.** Depende: J04, R02.
Arquivos (5): `tools/dotnet/Raiders.Tools.Common/{BundleDocumentStore.cs,
RecoveryJournal.cs,ExternalChangeDetector.cs}`,
`tests/dotnet/Raiders.Tools.Tests/DocumentSaveTests.cs`, `docs/editors.md`.
Entrega: transação de autoria com journal para membros/manifest. Aceite: falha
não destrói original; alteração externa gera conflito; backups não viram assets.

**J06 - Protocolo de tooling e cliente CLI C#.** Depende: J02, R03.
Arquivos (6): `schemas/tooling-protocol.schema.json`,
`tools/dotnet/Raiders.Tools.Contracts/{Raiders.Tools.Contracts.csproj,AssetDtos.cs}`,
`tools/dotnet/Raiders.Tools.Common/PipelineClient.cs`,
`tests/dotnet/Raiders.Tools.Tests/PipelineClientTests.cs`, `docs/tooling-protocol.md`.
Entrega: schema/DTOs, versão, request IDs, stdout estruturado, stderr diagnóstico,
cancelamento e limites. Aceite: versões incompatíveis/falha de processo são claras;
paths são argumentos, não comandos concatenados.

**J08 - C# client presents the native preview surface.** Depends: J07.
Paths (5): `tools/dotnet/Raiders.Tools.Common/{PreviewClient.cs,
PreviewSession.cs,PreviewSurface.cs}`, `tests/dotnet/Raiders.Tools.Tests/PreviewTests.cs`,
`docs/editors.md`.
Acceptance: host native window or present exact native-produced frames using a
versioned surface protocol; C# never re-rasterizes glyphs, primitives, sprites or UI.
Buffer ownership, lifetime, DPI, resize, timeout/backpressure and process cleanup
are tested. Editor overlays remain separate from the native content surface.

**K01 - Aplicação C# e palette de widgets.** Depende: J05, J06, H07.
Arquivos (6): `tools/dotnet/Raiders.UIEditor/{Raiders.UIEditor.csproj,Program.cs,
UiEditorDocument.cs,WidgetPalette.cs}`, `Raiders.Tools.sln`,
`tests/dotnet/Raiders.Tools.Tests/UiDocumentTests.cs`.
Entrega: criar/abrir bundle/tela, adicionar painel/texto/botão e salvar XML membro.
Aceite: IDs persistem; build do editor não exige compilar os demais editores.

**K02 - Canvas de layout em C#.** Depende: K01, J04.
Arquivos (5): `tools/dotnet/Raiders.UIEditor/{UiCanvas.cs,LayoutCommands.cs,
WidgetTree.cs,UiEditorDocument.cs}`, `tests/dotnet/Raiders.Tools.Tests/UiLayoutEditTests.cs`.
Aceite: drag/resize/reparent/ordem/anchors com undo; preview usa layout da engine,
sem depender de uma segunda implementação C# para comprovar fidelidade.

**K03 - Cabeçalho Lua com preservação de custom code.** Depende: K01, G08, R02.
Arquivos (6): `tools/authoring/{include/UiLuaGenerator.h,src/UiLuaGenerator.cpp}`,
`tools/authoring/CMakeLists.txt`, `tests/pipeline/ui_lua_generation_tests.cpp`,
`tests/fixtures/ui_custom.lua`, `docs/scripting.md`.
Entrega: gerador de pipeline C++ ou Python invocado pelo editor C#.
Aceite: versão, referências por ID e bloco custom final; regeneração idempotente
preserva bytes do bloco, comentários/Unicode; marcadores inválidos impedem sobrescrita.

**K04 - Save coordenado XML/Lua/manifest.** Depende: K02, K03, J05.
Arquivos (5): `tools/dotnet/Raiders.UIEditor/UiEditorDocument.cs`,
`tools/authoring/src/UiLuaGenerator.cpp`,
`tools/dotnet/Raiders.Tools.Common/BundleDocumentStore.cs`,
`tests/dotnet/Raiders.Tools.Tests/UiSaveTests.cs`, `docs/editors.md`.
Aceite: criar tela gera Lua membro; rename mantém ID e custom code; journal
recupera falha entre arquivos e mantém manifesto coerente com todos os membros.

**K05 - Handlers e preview de UI.** Depende: K04, H06, J08, V17.
Arquivos (4): `tools/dotnet/Raiders.UIEditor/{UiPreviewController.cs,
EventBindingInspector.cs,UiEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/UiPreviewTests.cs`.
Aceite: clique/tween/script rodam no host nativo; parar descarta estado de execução;
erro tem asset/callback/linha e não perde o documento.


## Integrated UI authoring

UI layout chooses a valid render layer; a default front layer is a template choice,
not a renderer rule. Native hit testing and preview must match the chosen visual
ordering. Save regeneration preserves custom Lua; open-code/debug commands share
the same Lua workspace and source maps used by other tools.

**K06 - Configurable UI layers and debugging integration.** Depends: K05, D09, G15.
Paths (5): `tools/dotnet/Raiders.UIEditor/{LayerInspector.cs,
EventBindingInspector.cs,UiPreviewController.cs,UiEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/UiLayerDebugTests.cs`.
Acceptance: UI can render behind/in front of gameplay according to authored order;
preview pixels and hit order match runtime; clicking a handler opens correct Lua
source and attaches to this preview session.

**K07 - UI performance and footprint capture.** Depends: K06, T15, S08.
Paths (4): `tools/dotnet/Raiders.UIEditor/{UiDiagnosticsPanel.cs,
UiPreviewController.cs}`, `tests/dotnet/Raiders.Tools.Tests/UiDiagnosticsTests.cs`,
`docs/editors.md`.
Acceptance: layout/update/render/handler timings and UI->script/module memory tree
can be captured; shared script costs are identified instead of multiplied per widget.
