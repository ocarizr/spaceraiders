# Assets, bundles, build, CI and distribution

Status: Q01-Q02 and Q05-Q06 merged; Q07 implemented and locally validated, pending
review/merge. The [build decision](../decisions/0001-monorepo-build.md), environment
policies, package policies and Python environment recipes are established;
remaining asset processing and selective orchestration remain planned.

## Scope and independent execution

C++20/Python noninteractive authoring services, schema/IR, bundles, validation,
cooking, build orchestration, selective CI and binary distributions. Managed
projects use .NET 10; reusable libraries support NuGet package boundaries and
Python modules use wheels/build/runtime environments.
Inputs: registered component schemas, LuaJIT ABI, rendering/animation schemas,
observability. Exports: assetc/tooling protocol, source/cooked bundles and Designer SDK.
First slice: author one source bundle and deserialize a new entity ID directly
in Development/PreviewHost, without cooking, packaging or native compilation.
Editing and packaging are distinct graphs. Full-game packaging generates eligible
C++ content and compiles it; source-data editing never needs that generation.

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

I05 uses the V09 native scheduler for incremental cooking dependencies. V18 adds
Python/subprocess orchestration over I06. O07 selects native and wrapper consumers;
O06 includes V19 combined-workload budgets. Editing never invokes that cook graph.

## Implementation backlog

**F01 - IDs, schemas e modelo intermediário.** Depende: C02, D02, E02.
Arquivos (6): `engine/assets/CMakeLists.txt`,
`engine/core/include/raiders/AssetId.h`, `engine/assets/include/raiders/AssetModel.h`,
`docs/assets.md`, `tests/assets/model_tests.cpp`, `CMakeLists.txt`.
Entrega: IDs estáveis separados de nomes, versão de schema e componentes suportados.
Aceite: duplicação de ID/referência inexistente gera erro; definir valores default.

**F02 - Dependência XML e importador de entidades.** Depende: F01.
Arquivos (6): `cmake/Dependencies.cmake`, `tools/authoring/CMakeLists.txt`,
`tools/authoring/{include/EntityXml.h,src/EntityXml.cpp}`,
`tests/assets/entity_xml_tests.cpp`, `tests/fixtures/entity.xml`.
Entrega: parser fixado, sem resolução externa de entidades/DTD, com diagnósticos
de arquivo/linha. Aceite: erro de tipo, schema futuro e campo desconhecido seguem
política explícita; only Shipping omits the XML parser; Development links the shared source-provider adapter.

**F03 - Exportação e round-trip de entidades.** Depende: F02.
Arquivos (4): `tools/authoring/include/EntityXml.h`,
`tools/authoring/src/EntityXml.cpp`, `tests/assets/entity_xml_tests.cpp`,
`tests/fixtures/entity_roundtrip.xml`.
Aceite: salvar/reabrir mantém semântica, IDs e referências; saída é estável,
atômica e não altera silenciosamente uma versão desconhecida.

**F04 - Schema e importação de layout UI.** Depende: F02.
Arquivos (5): `tools/authoring/{include/UiXml.h,src/UiXml.cpp}`,
`tools/authoring/CMakeLists.txt`, `tests/assets/ui_xml_tests.cpp`,
`tests/fixtures/ui_layout.xml`.
Entrega: árvore, IDs, anchors, dimensões, ordem, estilos e scripts/handlers.
Aceite: ciclos/referências quebradas e IDs duplicados são rejeitados.

**F05 - Exportação UI e grafo de dependências.** Depende: F03, F04.
Arquivos (6): `tools/authoring/include/UiXml.h`, `tools/authoring/src/UiXml.cpp`,
`tools/authoring/{include/AssetGraph.h,src/AssetGraph.cpp}`,
`tools/authoring/CMakeLists.txt`, `tests/assets/asset_graph_tests.cpp`.
Aceite: round-trip UI e resolução relativa à raiz de assets; caminhos que escapam
da raiz e diferenças de case têm erro; ciclos de ownership são rejeitados e
referências mútuas permitidas pelo schema são representadas no grafo.

**F06 - CLI de validação de assets.** Depende: F05.
Arquivos (5): `tools/assetc/{CMakeLists.txt,src/main.cpp}`,
`CMakeLists.txt`, `tests/assets/assetc_tests.cmake`, `docs/assets.md`.
Entrega: comando de uso geral com exit codes, localização de erros e modo validate.
Aceite: conjunto válido passa; erro em asset referenciado falha mesmo que o arquivo
raiz esteja correto. Nenhum script auxiliar exclusivo de agente é necessário.

**F07 - Serialização de sprites em grade.** Depende: F06, D02.
Arquivos (7): `tools/authoring/{include/SpriteXml.h,src/SpriteXml.cpp}`,
`tools/authoring/CMakeLists.txt`, `tools/authoring/src/AssetGraph.cpp`,
`engine/assets/include/raiders/AssetModel.h`, `tests/assets/sprite_xml_tests.cpp`,
`tests/fixtures/sprite.xml`.
Entrega: schema, importação/exportação, origem e células transparentes; validação
de dimensões, símbolos e cores. Aceite: round-trip é estável e entidade que
referencia sprite inválido falha na validação transitiva.

**F08 - Serialização de cenas e instâncias.** Depende: F07.
Arquivos (7): `tools/authoring/{include/SceneXml.h,src/SceneXml.cpp}`,
`tools/authoring/CMakeLists.txt`, `tools/authoring/src/AssetGraph.cpp`,
`engine/assets/include/raiders/AssetModel.h`, `tests/assets/scene_xml_tests.cpp`,
`tests/fixtures/scene.xml`.
Entrega: referências a entidades, overrides tipados e referências entre instâncias.
Aceite: validar/reabrir cena preserva IDs; referência circular de hierarquia ou
instância ausente produz erro antes da criação do mundo.

**I01 - Fixar ABI lógica dos descritores e registradores.** Depende: F01, C03, G04.
Arquivos (6): `engine/assets/include/raiders/{AssetDescriptors,ComponentRegistry}.h`,
`engine/assets/src/ComponentRegistry.cpp`, `engine/assets/CMakeLists.txt`,
`tests/assets/registry_tests.cpp`, `docs/assets.md`.
Entrega: representações C++ de entidades/UI/cenas, tipos de componente e versão de
binding, sem dependência do parser. Aceite: tipo/propriedade desconhecida falha com
diagnóstico; ordem de registro não muda IDs persistidos.

**I02 - Packaging C++ generator for entity data and bindings.** Depends: I01, F06, R02.
Paths (6): `tools/assetc/{include/CppEmitter.h,src/CppEmitter.cpp}`,
`tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`tests/assets/entity_codegen_tests.cpp`, `tests/fixtures/codegen_entity.xml`.
Acceptance: full-game packaging converts eligible entity descriptors/registrations
to deterministic, escaped C++ which compiles into the native binary. Editing loads
source XML directly and never invokes this generator. Content-only packaging may
use the binary bundle representation for the same supported data.

**I03 - Packaging UI descriptors and script references.** Depends: I02, H07, G08.
Paths (4): `tools/assetc/src/CppEmitter.cpp`,
`engine/assets/include/raiders/AssetDescriptors.h`,
`tests/assets/ui_codegen_tests.cpp`, `tests/fixtures/codegen_ui.xml`.
Acceptance: packaging generates eligible UI layout/binding descriptors as C++/
binary data; instantiated UI matches direct XML loading. Lua stays a source module
in editing and becomes a bytecode record during packaging, not native behavior.

**I04 - Packaging sprites, scenes and animation references.** Depends: I03, F08.
Paths (6): `tools/assetc/src/CppEmitter.cpp`,
`engine/assets/include/raiders/AssetDescriptors.h`,
`tests/assets/{scene_codegen_tests,sprite_codegen_tests}.cpp`,
`tests/fixtures/{codegen_scene,codegen_sprite}.xml`.
Acceptance: generate compilable native data/binary payloads for eligible scene/
sprite records with stable IDs and optimized layout; no machine paths. Editing
does not invoke the packaging emitter or require its outputs.

**I05 - Manifesto e grafo incremental de cooking.** Depende: I04, F05, R04, V09.
Arquivos (6): `tools/assetc/{include/CookManifest.h,src/CookManifest.cpp}`,
`tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`tests/assets/incremental_cook_tests.cpp`, `docs/assets.md`.
Entrega: hashes, versões do gerador/schema, depfile, remoção de saídas órfãs e
write-if-different. Aceite: mudar dependência invalida consumidor; apagar/renomear
asset não deixa registro velho; tocar arquivo sem alterar conteúdo não recompila tudo.

**I06 - Separate editing, content packaging and full-game packaging targets.** Depends: I05.
Paths (5): `cmake/CookAssets.cmake`, `CMakeLists.txt`, `game/CMakeLists.txt`,
`tools/assetc/CMakeLists.txt`, `tests/assets/build_graph_tests.cmake`.
Acceptance: Play/refresh loads serialized data and does not invoke cooker/codegen/
linker. Explicit content-only packaging emits binary bundles/bytecode without native
compile; full-game packaging cooks eligible data, generates C++ and compiles/links it.
Dependencies/byproducts support clean/parallel/incremental packaging independently.

**I07 - Shared entity/scene instantiation from typed descriptors.** Depends: I01, E03, G04, R09.
Paths (6): `engine/assets/{include/raiders/EntityFactory.h,src/EntityFactory.cpp,
include/raiders/SceneDescriptor.h}`, `engine/assets/CMakeLists.txt`,
`tests/assets/entity_factory_tests.cpp`, `tests/fixtures/factory_entity.xml`.
Acceptance: source deserialization and packaged data feed common instantiation;
forward references resolve after IDs exist; errors roll back partial entities.
This factory must work before cooking/C++ content generation is implemented.

**I08 - Editing versus packaged representation equivalence.** Depends: I07, H07, R05, I06.
Paths (5): `tests/integration/cooked_parity_tests.cpp`,
`tests/fixtures/{parity_entity,parity_ui}.xml`, `tests/CMakeLists.txt`, `docs/assets.md`.
Acceptance: load original XML directly, then explicitly package the same content
as binary bundles and generated/compiled C++; compare IDs, component state, Lua
events and native frames. Check decoded allocations/loading metrics separately:
equivalent behavior does not imply identical editing/Shipping memory footprints.

**I09 - Native type registry and loadable bundle catalogs.** Depends: I08.
Paths (5): `engine/assets/{include/raiders/AssetCatalog.h,src/AssetCatalog.cpp}`,
`engine/assets/CMakeLists.txt`, `tools/assetc/src/CppEmitter.cpp`,
`tests/assets/catalog_tests.cpp`.
Acceptance: binary fixes native component/binding schema, not a closed list of asset
IDs. Bundle catalog/content IDs may change without rebuilding the game; unsupported
schema/API fails early. Optional embedded records are not the only catalog source.

**O01 - Classificação de impacto e gate único.** Depende: A05, B07.
Arquivos (4): `.github/workflows/ci.yml`, `tools/pipeline/classify_changes.py`,
`tests/pipeline/test_ci_impact.py`, `docs/ci.md`.
Entrega: um workflow de PR sempre iniciado; classificador produz flags por domínio;
gate com `always()` aceita somente sucesso ou skip legitimado pelo plano de impacto.
Aceite: docs-only é verde, falha/cancelamento de job necessário bloqueia gate;
evento sem diff conhecido executa conjunto completo por segurança operacional.

**O02 - Jobs de runtime/jogo e matriz enxuta.** Depende: O01, B11.
Arquivos (4): `.github/workflows/ci.yml`, `CMakePresets.json`,
`tests/pipeline/test_ci_impact.py`, `docs/ci.md`.
Entrega: build/test Debug para PR; Release e segundo compilador conforme domínio
ou integração/release, sem multiplicar todo job por toda configuração.
Aceite: alteração em engine dispara consumidores afetados, não só unit tests locais.

**O03 - Jobs seletivos de assets, Lua e ferramentas C#.** Depende: O02, G08, I08, L04, S04, Q04.
Arquivos (4): `.github/workflows/ci.yml`, `tools/pipeline/classify_changes.py`,
`tests/pipeline/test_ci_impact.py`, `docs/ci.md`.
Entrega: jobs validate-assets, scripting e build/test .NET por projeto, incluindo
inspetor, com restore fixado e cache por SDK/toolchain/locks. Aceite: edição apenas de gameplay Lua não compila editores;
schema/importador/binding compartilhado valida dependentes transitivos.

**O04 - Checagens estáticas e diagnósticos de memória.** Depende: O02, C03.
Arquivos (6): `.clang-format`, `.editorconfig`, `cmake/Quality.cmake`,
`CMakePresets.json`, `.github/workflows/ci.yml`, `docs/development.md`.
Entrega: formatação C++/C# por arquivos alterados, analyzers .NET e análise/sanitizer compatível com a
toolchain escolhida; warnings por target. Aceite: não aplicar reformat global no
mesmo PR; adotar por lotes de até 8 fontes, em sub-PRs, se houver legado divergente.

**O05 - Testes do fluxo real de autoria e distribuição.** Depende: O03, K05, L05, P03.
Arquivos (6): `tests/integration/{ui_authoring_tests.cpp,entity_authoring_tests.cpp,
packaged_game_tests.cmake}`, `tests/CMakeLists.txt`, `.github/workflows/ci.yml`,
`docs/testing.md`.
Aceite: criar/salvar/reabrir asset, customizar Lua, regenerar, cookar e iniciar
pacote usando serviços reais dos editores; complementar com smoke manual da
interação visual, sem fingir que teste de serialização valida toda a interface.

**O06 - Desempenho medido e orçamento simples.** Depende: N05, P03, V19.
Arquivos (5): `tests/integration/performance_scenarios.cpp`,
`tests/CMakeLists.txt`, `CMakePresets.json`, `docs/performance.md`,
`.github/workflows/ci.yml`.
Entrega: medir tick, apresentação, memória, startup e tamanho do pacote em cenários
fixos; resultados temporários ficam como artefato CI/local, não no fonte.
Aceite: escolher limites a partir de medidas; execução ampla em release/manual,
sem teste de tempo frágil bloquear todo PR em runner compartilhado.

**P01 - Development source-data and Shipping binary profiles.** Depends: I09, G08, R06, R09.
Paths (5): `CMakePresets.json`, `cmake/BuildProfiles.cmake`,
`CMakeLists.txt`, `game/CMakeLists.txt`, `docs/distribution.md`.
Acceptance: Development/PreviewHost includes source-provider XML deserializers,
source Lua/reload/debug and does not require cooking. Shipping includes optimized
binary/compiled providers and packaged bytecode, omits XML parser/authoring tools.
Both use the same renderer, simulation and registered component semantics.

**P02 - Install e conteúdo do pacote.** Depende: P01, M07.
Arquivos (5): `cmake/Install.cmake`, `CMakeLists.txt`, `game/CMakeLists.txt`,
`tests/integration/install_tests.cmake`, `docs/distribution.md`.
Entrega: executável, dependências nativas, bundles cooked com registros Lua,
catálogo/manifesto e licenças;
distribuição .NET das ferramentas é separada, em P07. Aceite: lista permitida exclui XML,
fontes C++, caches, logs, backups, fixtures e arquivos exclusivos de agentes.

**P03 - Smoke do pacote fora da árvore de fontes.** Depende: P02, N05.
Arquivos (5): `tests/integration/package_smoke.cmake`, `tests/CMakeLists.txt`,
aplicação do jogo, `cmake/Install.cmake`, `docs/distribution.md`.
Entrega: modo headless/bounded de uso geral para diagnóstico e CI, com script
de cenário, seed e ticks explícitos. Aceite: pacote em diretório limpo roda ambos
os modos sem XML/source tree; registro Lua/bundle ausente ou de geração incompatível falha com erro claro.

**P04 - Empacotamento versionado.** Depende: P03.
Arquivos (4): `cmake/Packaging.cmake`, `CMakeLists.txt`,
`tests/integration/package_content_tests.cmake`, `docs/distribution.md`.
Entrega: archive de release com versão, manifesto/hash e avisos de dependências.
Aceite: builds com mesmos inputs têm conteúdo lógico equivalente; timestamps,
paths e metadados variáveis são controlados antes de prometer ZIP byte a byte idêntico.

**P05 - Workflow de release.** Depende: P04, O03, P07.
Arquivos (3): `.github/workflows/release.yml`, `docs/distribution.md`,
`tests/pipeline/test_ci_impact.py`.
Entrega: build limpo, validação completa, cooking, testes e pacote em tag/manual.
Aceite: falha impede publicação; PR não publica release; permissões de escrita
restritas ao job de publicação. O ato de publicar segue autorização do mantenedor.

**P06 - Guia de desenvolvimento e entrega final.** Depende: O05, O06, P05, S11, R07, Q03, W06, T14, G17, N07, L08, K07, X07, U14, Z09, Y14.
Arquivos (até 6): `README.md`, `docs/{development,editors,scripting,
assets,distribution}.md`.
Entrega: caminho completo de checkout a build, autoria, testes, cooking e pacote;
troubleshooting real e limitações da engine. Aceite: desenvolvedor em ambiente
limpo reproduz tudo sem configurações pessoais ou instruções locais do agente.

**P07 - Independent binary Designer SDK distribution.** Depends: J08, S06, Q03, W01, X03.
Paths (6): `tools/pipeline/package_tools.py`, `tests/pipeline/test_tools_package.py`,
`tools/dotnet/Directory.Build.targets`, `tests/integration/tools_package_smoke.py`,
`docs/distribution.md`, `.github/workflows/release.yml`.
Acceptance: package chosen C# apps, native PreviewHost/content CLI, LuaJIT compiler,
schema/API definitions, templates and version manifest; no game rebuild required
for a tools-only artifact. Python tools ship frozen/runtime-bundled if selected.
No Visual Studio, native/.NET SDK or Python installation needed on designer machine.
VS Code is an optional external prerequisite for Lua editing/debugging, with clear setup.

**Q01 - Windows monorepo build decision.** Depends: A01.
Paths (2): `docs/build-system.md`, `docs/decisions/0001-monorepo-build.md`.
Delivery: C++20/CMake and Bazel native generation/incremental spikes; .NET 10
NuGet/desktop and Python wheel/venv checks; compare Windows project/IDE/build ownership.
Acceptance: recommendation, alternatives, primary sources and evidence limits are
documented without production migration. Implementation of SDK pins/packages/setup
remains Q02-Q07 and Y01-Y07, one task at a time.

**Q02 - Pin .NET 10, native and Python environment policies.** Depends: Q01.
Paths (6): `global.json`, `Directory.Build.props`, `Directory.Packages.props`,
`NuGet.config`, `pyproject.toml`, `.gitignore`.
Acceptance: .NET major 10 is required; choose verified SDK patch/roll-forward,
managed package/lock policy and supported Python/native tool versions. Ignore local
tools, interpreters, package outputs and venvs. Do not require .NET for a native
target unless it truly consumes a managed artifact. CMake does not generate C# projects.

**Q03 - Solution e filtros por ferramenta.** Depende: J02, K01, L01, S01, W01.
Arquivos (6): `Raiders.Tools.sln`, `tools/dotnet/{UIEditor,EntityEditor,
BundleInspector,LevelEditor}.slnf`, `docs/build-system.md`.
Entrega: workspaces de cada aplicação e dependências reais, sem abrir todo o
monorepo. Aceite: IDE/debug e build de cada .csproj/filtro funcionam separadamente.

**Q04 - CLI Python do monorepo por projeto.** Depende: Q01, Q02, Q06, J02.
Arquivos (5): `tools/pipeline/build.py`, `tools/pipeline/projects.json`,
`tests/pipeline/test_build.py`, `pyproject.toml`, `docs/build-system.md`.
Entrega: build/test/cook/package por projeto, delegando a CMake ou dotnet; seleção
de targets e argumentos, sem reconstruir o grafo que já pertence a esses sistemas.
Aceite: engine não constrói C#; editor não constrói outros editores; falha nativa
ou .NET retorna código não zero. É ferramenta de desenvolvedor, não helper de agente.

**R01 - Contrato de bundle e identidade.** Depende: F01.
Arquivos (4): `docs/data-bundles.md`, `schemas/bundle-manifest.schema.json`,
`engine/assets/include/raiders/BundleId.h`, `tests/fixtures/bundle-manifest.json`.
Entrega: autoria diffável, IDs persistentes, referências entre bundles, versões,
tipos, hashes e formato cooked. Definir quais ciclos são válidos; separar ownership
de referência. Aceite: XML/Lua são membros registrados, formato binário não é dump
de structs e scripts mantêm identidade nas pastas de UI/gameplay dentro de bundles.

**R02 - Leitura/save transacional de bundle de autoria.** Depende: R01, F08.
Arquivos (6): `tools/authoring/{include/SourceBundle.h,src/SourceBundle.cpp}`,
`tools/authoring/CMakeLists.txt`, `tests/assets/source_bundle_tests.cpp`,
`tests/fixtures/sample.bundle/manifest.json`, `tools/assetc/src/main.cpp`.
Entrega: membros, manifesto, validação integrada e journal/recovery multi-file.
Aceite: falha/interrupção não publica bundle parcialmente atualizado; paths relativos,
case, IDs duplicados e membro não declarado são diagnosticados. Fixtures adicionais
contam no orçamento; desdobrar testes em sub-PR se necessário.

**R03 - Consultas de metadados/grafo por CLI.** Depende: R02, F05.
Arquivos (6): `tools/authoring/{include/BundleQueries.h,src/BundleQueries.cpp}`,
`tools/authoring/CMakeLists.txt`, `tools/assetc/src/main.cpp`,
`schemas/bundle-query.schema.json`, `tests/assets/bundle_query_tests.cpp`.
Entrega: list/get/dependencies/referencers com IDs, tipo de aresta e paginação.
Aceite: resultado determinístico inclui consumidores em outros bundles; consulta
não carrega/executa Lua nem requer rodar o jogo.

**R04 - Cooked bundle writer with script profile.** Depends: R02, I04, G08.
Paths (6): `tools/assetc/{include/BundleWriter.h,src/BundleWriter.cpp}`,
`tools/assetc/CMakeLists.txt`, `tools/assetc/src/main.cpp`,
`tests/assets/bundle_writer_tests.cpp`, `docs/data-bundles.md`.
Acceptance: deterministic indexed records/hashes/dependencies; development supports
packaging-debug metadata when requested; final Shipping uses G16 LuaJIT bytecode; no raw XML.
Native-schema compatibility and content catalog identity are distinct fields, so
a changed bundle does not force recompiling an unchanged compatible executable.

**R05 - Leitor binário C++20.** Depende: R04, I01.
Arquivos (6): `engine/assets/{include/raiders/BundleReader.h,src/BundleReader.cpp,
include/raiders/BundleFormat.h}`, `engine/assets/CMakeLists.txt`,
`tests/assets/bundle_reader_tests.cpp`, `tests/fixtures/corrupt.rdb`.
Aceite: rejeitar truncamento, overflow/offset inválido, versão desconhecida,
hash divergente e ID duplicado antes de acesso; não executar código ao inspecionar.

**R06 - Mountable catalogs and record lifetime.** Depends: R05, I09.
Paths (6): `engine/assets/{include/raiders/BundleStore.h,src/BundleStore.cpp}`,
`engine/assets/src/AssetCatalog.cpp`, `engine/assets/CMakeLists.txt`,
`engine/scripting/src/ScriptResolver.cpp`, `tests/assets/bundle_lifetime_tests.cpp`.
Acceptance: mount dependency catalogs, validate native-schema/VM compatibility,
resolve script records and protect live record handles through unload.
New content IDs are accepted without a compiled list; reject mismatched schema
or invalid references, not simply a changed content hash.

**R07 - Migração de versões e cooking incremental.** Depende: R06, I05.
Arquivos (5): `tools/authoring/{include/BundleMigration.h,src/BundleMigration.cpp}`,
`tools/authoring/CMakeLists.txt`, `tests/assets/bundle_migration_tests.cpp`,
`docs/data-bundles.md`.
Aceite: migração de autoria tem versão, backup e saída determinística; runtime
não tenta converter schema desconhecido; dependência modificada invalida somente
bundles afetados; script sem mudança de binding não força recompilação C++.

**R08 - Inspeção de bundle cooked pelo pipeline.** Depende: R03, R05.
Arquivos (5): `tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`schemas/bundle-query.schema.json`, `tests/assets/cooked_query_tests.cpp`,
`docs/data-bundles.md`.
Aceite: mesmas consultas funcionam em autoria/cooked; metadados mostram hash,
tipo, tamanho, offsets, origem e arestas; bundle inválido gera erro sem executar jogo.

## Selective CI: regras de seleção de CI

A seleção é por **impacto transitivo**, não somente extensão ou pasta alterada.
Jobs de tooling só rodam se a ferramenta ou algum contrato que ela consome mudou.
O gate obrigatório deve ter nome estável configurado na proteção da branch.

| Mudança | Jobs necessários | Jobs normalmente dispensados |
| --- | --- | --- |
| Somente Markdown, CODEOWNERS, template PR | Classificação e gate; checagem documental leve se adotada | Builds de engine, jogo e editores |
| Core, runtime ou contratos de componentes | Build/test do domínio e consumidores: jogo, scripting e ferramentas afetadas | Só domínios comprovadamente independentes |
| Rendering/console | Renderer, input/UI e smoke do jogo; editores se contrato compartilhado mudar | Cook de semântica de entidades independente |
| Física | Física, bindings Lua e cenários do jogo; inspector/preview se API mudar | Editor UI independente |
| Scripts gameplay | Sintaxe/módulos, bindings e cenários de gameplay afetados | Compilação dos editores e testes exclusivos de UI |
| Layout/scripts UI | Validação UI/Lua, cooking de UI e integração de tela | Editor de entidades e física independentes |
| Entidades/sprites/cenas XML | Validação, cooking e instanciação/cenário consumidor | Compilação de editor não alterado |
| Authoring, schemas, assetc ou gerador | Testes de tooling, validação de todos os assets afetados, cooking/paridade e consumidores | Nenhum consumidor do contrato alterado |
| Editor UI, entidades ou inspetor C# | dotnet build/test do projeto alterado e contratos comuns afetados | Outros frontends se contrato comum não mudou; jogo se runtime não mudou |
| Bundles, grafo persistido ou loader | Validação de manifest/membros, writer/reader, cooking, paridade, inspeção e cenário consumidor | Frontends independentes quando só conteúdo mudou |
| SDK .NET/NuGet/Directory.Build | Restore/build/test das ferramentas C# afetadas | Build nativo se contrato nativo não mudou |
| Pipeline Python/pyproject | Testes da CLI/geração/classificação e seus consumidores reais | UI C# se protocolo/outputs não mudaram |
| CMake/dependências/workflows | Configuração e checks das áreas que o arquivo controla; global implica conjunto amplo | Apenas áreas de independência comprovada |
| Packaging/install | Build Shipping, conteúdo/install e smoke do pacote | Interação dos editores se ausentes do pacote |
| Rename/delete, path desconhecido, classificador alterado | Fallback completo e teste do classificador | Nada por suposição |

Implementar diff contra o merge-base e considerar ambos os caminhos em renames.
Não confiar em `paths` no workflow inteiro para um check obrigatório: ele pode
nem existir para a proteção de branch. Classificador sempre roda, jobs usam flags,
gate inspeciona flags e resultados. `skipped` não é sucesso automático quando um
job necessário foi pulado por dependência falha. Run cancelado também não aprova.
Não executar código de PR não confiável num contexto com secrets de publicação.

Poucos arquivos de workflow bastam: `ci.yml` para PR/integração e `release.yml`
para distribuição. Jobs reutilizam targets/presets; não criar uma pipeline para
cada componente. Suite completa em integração à `master` e release cobre lacunas
da seleção; execuções manuais permitem diagnóstico sem mudar código.



## Editing and packaging are separate runtime paths

Editing works directly with serialized XML/Lua in source bundles. A development
source-provider adapter reuses the authoring parser/deserializers and creates
in-memory typed descriptors/live objects for the normal engine systems.
Deserialization may cost more CPU and memory; that is an accepted editing tradeoff.
Do not create temporary cooked bundles, generated C++ or offline bytecode caches
as a prerequisite to Save/Play/reload. Ordinary in-memory loading is not cooking.

Packaging is explicit and still mandatory for distribution optimization. Validate
and convert eligible entity/UI/scene/animation data into efficient versioned binary
layouts, references and resource payloads. Full-game packaging includes generated
C++ content where appropriate and compiles/links that into the game's native binary.
Content-only packaging can emit compatible binary bundles without rebuilding it.
These are packaging profiles, not reasons to restrict binary generation to schemas
or to force the editor through cooking.

Use one shared instantiation contract after either provider loads content. Source
adapters/deserializers are Development/PreviewHost dependencies; Shipping excludes
them. A packaged data provider handles binary/generated native descriptors, preserving
semantic parity while allowing more efficient loading, allocation and data layout.
The designer workstation edits/playtests with binaries only; a full native packaging
job can run on a developer/CI machine with the necessary native toolchain.
The Designer SDK includes authoring templates, not a requirement to compile source.

**F09 - Drawing/image resource schema and authoring codec.** Depends: F07, D07.
Paths (6): `schemas/drawing.schema.json`,
`tools/authoring/{include/DrawingXml.h,src/DrawingXml.cpp}`,
`tools/authoring/CMakeLists.txt`, `engine/assets/include/raiders/AssetModel.h`,
`tests/assets/drawing_roundtrip_tests.cpp`.
Acceptance: pen/primitive data, image imports and references round-trip as bundle
records; native render metadata and decoded-size estimates have explicit semantics.

**F10 - Animation clip/track schema and cooking.** Depends: F09, D08.
Paths (6): `tools/authoring/{include/AnimationXml.h,src/AnimationXml.cpp}`,
`tools/authoring/CMakeLists.txt`, `engine/assets/include/raiders/AssetModel.h`,
`tools/assetc/src/BundleWriter.cpp`, `tests/assets/animation_roundtrip_tests.cpp`.
Acceptance: sprite frames, transform keys, loop/duration/events and references
deserialize/validate for preview; packaging cooks them separately. Unknown targets/
properties fail before preview; no timeline-to-C++ rebuild during editing.

**X01 - Designer project/SDK compatibility manifest.** Depends: R01, G10, Q02.
Paths (4): `schemas/designer-project.schema.json`,
`tools/authoring/include/ProjectCompatibility.h`,
`tests/assets/project_compatibility_tests.cpp`, `docs/designer-quickstart.md`.
Acceptance: project source bundles, entry scene, native schema, VM/compiler and
tool protocol versions declared; report required runtime upgrades without attempting
native compilation on the designer's machine.

**X02 - Source-data runtime path for newly authored content.** Depends: R09, I07, G10.
Paths (6): `engine/assets/{include/raiders/EntityFactory.h,src/EntityFactory.cpp,
src/AssetCatalog.cpp}`, `engine/ui/src/UiFactory.cpp`,
`tests/assets/dynamic_content_tests.cpp`, `docs/data-bundles.md`.
Acceptance: an existing Development/PreviewHost directly deserializes new XML/
Lua members into player/UI/scene objects using registered types. No cooked data,
generated translation unit, bytecode artifact or compiler is present in this test.
Shipping loads the equivalent packaged representation in its separate path.

**X03 - Tools-only build/test/package entry.** Depends: Q04, W01, S01.
Paths (5): `tools/pipeline/{build.py,projects.json}`,
`tests/pipeline/test_tools_build.py`, `.github/workflows/ci.yml`,
`docs/build-system.md`.
Acceptance: select tools products in manual/CI runs; build required engine libraries,
PreviewHost and asset CLI, not the unrelated sample executable or all test matrices.
SDK/compilers are CI/developer requirements only; output is binary tooling artifact.

**X04 - Export designer workspace and VS Code setup.** Depends: P07, G15, X01.
Paths (5): `tools/pipeline/create_designer_workspace.py`,
`tests/pipeline/test_designer_workspace.py`,
`tools/dotnet/Raiders.Tools.Common/ProjectSetupService.cs`,
`tests/dotnet/Raiders.Tools.Tests/ProjectSetupTests.cs`,
`docs/designer-quickstart.md`.
Acceptance: distributed binaries create templates/bundles, paths, LuaLS stubs,
VS Code tasks/debug settings and instructions; no source repo checkout or compiler
is needed. Do not silently install VS Code/extensions; licensing/pinning documented.

**X05 - Validate, deserialize and reload for editing.** Depends: X02, J08, G15, R02.
Paths (5): `tools/dotnet/Raiders.Tools.Common/{SourcePreviewClient.cs,
ProjectCommands.cs}`, `tools/assetc/src/main.cpp`,
`tests/integration/designer_preview_tests.py`, `docs/designer-quickstart.md`.
Acceptance: tools open saved source-bundle XML/Lua and pass validated source/typed
in-memory data to Development/PreviewHost; validation errors open file/line.
Failure keeps last-good live state. Assert zero cooker/codegen/linker invocations
and no cooked outputs/cache on Play/reload; native Lua source loading is not an
offline bytecode-cooking step.

**X06 - Explicit packaging profiles for authored content.** Depends: X05, G16, R07, I06.
Paths (5): `tools/assetc/src/main.cpp`, `tools/assetc/src/BundleWriter.cpp`,
`tests/integration/content_update_tests.py`, `cmake/Install.cmake`,
`docs/distribution.md`.
Acceptance: Package triggers validation/cooking of serialized data into binary
representations and Lua bytecode. Content-only packaging emits bundles for a
compatible existing executable; full-game packaging additionally generates and
compiles eligible C++ content. Neither operation is a prerequisite for editing.

**X07 - Clean-machine binary workflow acceptance.** Depends: X04, X06, G14, S11.
Paths (4): `tests/integration/designer_sdk_tests.py`,
`tests/fixtures/designer-project.bundle/manifest.json`,
`docs/designer-quickstart.md`, `.github/workflows/ci.yml`.
Acceptance: machine with binaries and optional configured VS Code, but no native/
.NET/Python SDKs, can author, lint, break/step/watch Lua, inspect footprint and
install a cooked content update. Noninteractive tests plus recorded manual IDE
acceptance; publish no temporary captures into the source tree.

**O07 - Selective CI for diagnostics and shared surfaces.** Depends: O03, T05, T06, D04, V12, V14.
Paths (4): `tools/pipeline/classify_changes.py`,
`tests/pipeline/test_ci_impact.py`, `.github/workflows/ci.yml`, `docs/ci.md`.
Acceptance: native diagnostics/surface changes test affected consumers; C# changes
build affected tools only; Python-only pipeline and Lua-only edits skip unrelated
frontends; unsupported diff classification falls back to full required set.

**O08 - Lua IDE/bytecode and tools-only pipeline checks.** Depends: O07, G12, G16, X03.
Paths (4): `.github/workflows/ci.yml`, `tests/pipeline/test_ci_impact.py`,
`tests/integration/lua_toolchain_tests.py`, `docs/ci.md`.
Acceptance: source lint/API definitions, embedded debug contract, debug/Shipping
bytecode parity and tools-only artifacts are selectable jobs; one mandatory gate
correctly handles necessary skips/failures. No one-workflow-per-component sprawl.


**R09 - Development-only serialized source provider.** Depends: R02, F08, I01.
Paths (6): `tools/authoring/{include/SourceAssetProvider.h,src/SourceAssetProvider.cpp}`,
`tools/authoring/CMakeLists.txt`,
`engine/assets/include/raiders/AssetProvider.h`,
`tests/assets/source_provider_tests.cpp`, `docs/data-bundles.md`.
Acceptance: reuse XML/Lua authoring deserializers to load entity/UI/scene definitions
directly for live editing/playtest, resolving source-bundle IDs and dependencies.
Source and packaged providers implement common lookup/instantiation semantics.
Works with no cooker/generated/binary outputs; parser is absent from Shipping,
not banned from the development runtime. Source errors preserve last-good state.


## Package foundations and setup handoff

Q01 selects [CMake/native + .NET 10/MSBuild](../build-system.md), without replacing
the language-specific graphs. The coordinator builds package producers and restores
their intended artifact versions for consumers. Python wheels and installed module
entry points are part of the same developer-facing workflow. The independent
[environment setup plan](environment-setup.md) owns the C# GUI, probes and repair;
these follow-up tasks do not implement that UI.

**Q05 - Reusable NuGet library build/restore policy.** Depends: Q02.
Paths (6): `Directory.Build.targets`, `tools/dotnet/build/PackDependencies.targets`,
`NuGet.config`, `tests/dotnet/Raiders.Build.Tests/{Raiders.Build.Tests.csproj,
PackageGraphTests.cs}`, `docs/dependencies.md`.
Acceptance: selected library producers create local/internal .nupkg artifacts;
consumers use explicit PackageReference versions, central management and locked
restore. Test transitive package order and cache invalidation using immutable
versions; no loose DLL copying, duplicate BUILD graph or public-feed publication.
Add small fixture projects in sub-deliveries if the total would exceed 10 paths.

**Q06 - Python module wheel and installed entry point.** Depends: Q02.
Status: implemented and locally validated on `Q06_python_wheel_build_and_entry_point`,
pending review/merge. See [wheel module usage](../dependencies.md#python-wheel-module-and-installed-entry-point-q06).
Paths (6 implementation/package/test files): `tools/pipeline/wheel_build/pyproject.toml`,
`tools/pipeline/wheel_build/src/raiders_pipeline/{__init__.py,__main__.py,build_wheels.py,wheel_policy.json}`,
`tools/pipeline/wheel_build/tests/test_wheel_install.py`; CI/docs complete the delivery.
Acceptance: reusable pipeline module has proper package metadata/namespace, builds
a wheel in its prepared build environment and runs from a separate installed-package
environment without sys.path hacks or dependence on source checkout location.
Include needed data files/entry points; test the actual wheel, not just source imports.

**Q07 - Reproducible Python environment recipes.** Depends: Q02, Q06.
Status: implemented and locally validated on `Q07_reproducible_python_environments`,
pending review/merge. See [recipes and bootstrap contract](../dependencies.md#reproducible-python-environments-q07).
Paths (4 implementation/recipe/test files):
`tools/pipeline/wheel_build/src/raiders_pipeline/environments/{python-build,python-tools}.lock`,
`tools/pipeline/wheel_build/src/raiders_pipeline/create_env.py`,
`tools/pipeline/wheel_build/tests/test_environment_recipes.py`; CLI/package metadata,
CI/docs complete the delivery. Native installer acquisition metadata is available
to the future C# setup manager; Q07 discovers existing compatible interpreters.
Acceptance: discover or acquire pinned compatible interpreter, recreate build/runtime
venvs from locked artifacts/hashes, validate installed package fingerprints and report
Ready/Needs repair. Source-owned recipes are separate from ignored local environments;
the C# setup manager can execute them without assuming Python was already installed.
