# C# bundle, dependency and memory inspector

Status: Planned. Implementation has not started.

## Scope and independent execution

C# browser of authored/cooked bundles, direct/reverse/transitive dependencies,
runtime snapshots and memory attribution views.
Inputs: bundle-query metadata, observed script dependencies and memory snapshots.
Exports: graph queries, selection/navigation and comparable footprint reports.
First slice: select player or a UI screen, expand entity/script/module dependencies
and distinguish estimated bytes from measured exclusive/shared/resident memory.
Never equate compressed bundle size with RAM or count shared allocations twice.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Implementation backlog

**S01 - Aplicação C# e browser de bundles.** Depende: J06, R03.
Arquivos (6): `tools/dotnet/Raiders.BundleInspector/{Raiders.BundleInspector.csproj,
Program.cs,BundleInspectorDocument.cs,BundleBrowser.cs}`, `Raiders.Tools.sln`,
`tests/dotnet/Raiders.Tools.Tests/BundleBrowserTests.cs`.
Aceite: abrir bundle, buscar asset por nome/ID/tipo e listar seus metadados, sem
carregar scripts; frontend é C# e usa o mesmo cliente de protocolo dos editores.

**S02 - Modelo de grafo e referências reversas.** Depende: S01.
Arquivos (5): `tools/dotnet/Raiders.BundleInspector/{DependencyGraph.cs,
GraphNode.cs,GraphEdge.cs,GraphQueryService.cs}`,
`tests/dotnet/Raiders.Tools.Tests/DependencyGraphTests.cs`.
Aceite: arestas diretas/reversas, ownership e referências entre bundles são distintas;
referências mútuas são agrupadas sem recursão infinita.

**S03 - Canvas de grafo navegável em C#.** Depende: S02.
Arquivos (5): `tools/dotnet/Raiders.BundleInspector/{GraphCanvas.cs,
GraphLayout.cs,GraphSelection.cs,GraphViewport.cs}`,
`tests/dotnet/Raiders.Tools.Tests/GraphNavigationTests.cs`.
Entrega: pan/zoom/seleção, labels, direção, filtro por tipo/bundle e expansão sob
demanda. Aceite: asset ligado a entidades é visível e selecionável; limitar nós
renderizados sem truncar silenciosamente resultados; UI não bloqueia em grafo grande.

**S04 - Inspeção de registros cooked e diagnósticos.** Depende: S03, R08.
Arquivos (5): `tools/dotnet/Raiders.BundleInspector/{RecordInspector.cs,
BundleComparison.cs,BundleInspectorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/RecordInspectorTests.cs`, `docs/bundle-inspector.md`.
Aceite: origem, tipo, versão, tamanho/hash e referência quebrada são exibidos;
comparar autoria/cooked por IDs; erro/corrupção preserva documento anterior.

**S05 - Caminhos e impacto de alterações.** Depende: S04.
Arquivos (4): `tools/dotnet/Raiders.BundleInspector/{GraphQueryService.cs,
DependencyPathView.cs,ImpactView.cs}`,
`tests/dotnet/Raiders.Tools.Tests/ImpactQueryTests.cs`.
Aceite: responder 'do que depende?', 'quem usa?' e caminho entre asset e entidade,
com dependências transitivas; ciclos e páginas incompletas são indicados claramente.

**S06 - Integração com os editores e guia.** Depende: S05, L05.
Arquivos (5): `tools/dotnet/Raiders.Tools.Common/AssetNavigation.cs`,
`tools/dotnet/Raiders.BundleInspector/BundleInspectorDocument.cs`,
`tests/dotnet/Raiders.Tools.Tests/AssetNavigationTests.cs`,
`docs/bundle-inspector.md`, `docs/editors.md`.
Aceite: abrir fonte selecionada no editor apropriado, ou diagnóstico se só houver
bundle cooked; navegação preserva ID e custom code. Inspetor não altera payload
binário diretamente; edições passam por autoria, validação e novo cooking.


## Memory inspection semantics

Static footprint from bundle metadata is an estimate of known decoded data.
Runtime snapshots report actual live allocations/capacity and observed dependencies
for a particular process/VM/session. Compressed disk size, decoded asset bytes,
per-instance components, shared VM/JIT caches and whole-process RAM are different
numbers. The UI must label units, coverage, epoch and unknown values.

For a selected player or UI, show direct/exclusive, inclusive unique, shared and
incremental costs. Traverse module/asset dependencies with identity-based visited
sets and cycle groups. Two references to one allocation are one resident cost;
two instances or two VM copies can be two allocations even if content hashes match.
Do not claim a retained-size/dominator view unless ownership/rooting data exists.
Display unattributed process memory and tracking overhead rather than forcing the
graph to equal a working-set total. Dynamic require edges are marked observed,
not a proof that all possible imports have been statically discovered.

**S07 - Static decoded-memory estimates in asset metadata.** Depends: S05, T01, R03.
Paths (5): `tools/authoring/src/BundleQueries.cpp`,
`schemas/bundle-query.schema.json`,
`tools/dotnet/Raiders.BundleInspector/FootprintEstimate.cs`,
`tests/assets/footprint_estimate_tests.cpp`, `docs/memory-model.md`.
Acceptance: distinguish stored/compressed/decoded bytes and per-instance estimates;
unsupported resource types return unknown, not zero.

**S08 - Runtime snapshot client/import.** Depends: S07, T12, J06.
Paths (5): `tools/dotnet/Raiders.BundleInspector/{MemorySnapshotDocument.cs,
MemorySnapshotClient.cs,MemorySnapshotValidator.cs}`,
`tests/dotnet/Raiders.Tools.Tests/MemorySnapshotTests.cs`, `docs/bundle-inspector.md`.
Acceptance: load saved or capture live snapshot; schema/build/session IDs, limits,
coverage and timestamps validated; capture failure preserves prior document.

**S09 - Inclusive/shared/incremental memory graph.** Depends: S08.
Paths (5): `tools/dotnet/Raiders.BundleInspector/{MemoryAttribution.cs,
MemoryGraphView.cs,GraphQueryService.cs}`,
`tests/dotnet/Raiders.Tools.Tests/MemoryAttributionTests.cs`, `docs/memory-model.md`.
Acceptance: one shared module referenced by player and UI is counted once per VM;
cycles terminate; multiple instances and caches remain distinguishable; a documented
fixture proves totals/deltas without summing incompatible memory categories.

**S10 - Snapshot diff, leaks and unclassified memory.** Depends: S09.
Paths (5): `tools/dotnet/Raiders.BundleInspector/{SnapshotDiff.cs,
MemorySummaryView.cs,AllocationDetailsView.cs}`,
`tests/dotnet/Raiders.Tools.Tests/SnapshotDiffTests.cs`, `docs/bundle-inspector.md`.
Acceptance: compare before/after spawn/despawn/load/unload; show native/managed/Lua/
JIT/allocator buckets and unexplained process delta; GC timing and partial captures
are visible, so a transient increase is not automatically labeled a leak.

**S11 - Performance capture navigation and large-graph UX.** Depends: S10, T15.
Paths (5): `tools/dotnet/Raiders.BundleInspector/{CaptureNavigation.cs,
MemoryGraphView.cs,ImpactView.cs}`,
`tests/dotnet/Raiders.Tools.Tests/DiagnosticsNavigationTests.cs`,
`docs/bundle-inspector.md`.
Acceptance: selected entity/widget/asset links snapshot, trace session and source;
queries/layout are cancellable and virtualized; loading a large trace/graph does not
freeze the tool being diagnosed.
