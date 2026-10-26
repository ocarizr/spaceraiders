# Engine, rendering, physics and native UI

Status: A01-A07 baseline merged. A08 is locally validated, pending review/merge;
engine extraction and runtime extensions remain planned.

## Scope and independent execution

The [addons plan](addons.md) owns native lifecycle/loading (U03), service/event
registration (U04) and addon asset/Lua integration (U09/U10). Native APIs must
permit these hooks without game-specific core edits; existing native tasks remain
incremental foundations. U12 tests future platform-provider seams using a fake
provider, while Windows remains the only supported real platform.

C++20 reusable runtime, Application entry point, World/components, native rendering,
2D physics, native UI and the preview host. No gameplay-specific types belong here.
Inputs: observability contracts; asset/schema contracts; Lua runtime interfaces.
Exports: engine_entry, raiders_runtime, raiders_render, raiders_physics, raiders_ui,
native surface/viewport API and PreviewHost. The console backend is a legacy adapter;
the final 2D sample needs a window/offscreen surface for rotated drawings and sprites.
First slice: reproduce legacy build, isolate ownership/loop, then provide a native
offscreen frame used by both the standalone game and editor preview.
Acceptance: a second application links without Space Raiders; identical scene,
camera, viewport and simulation time produce identical native frames in both hosts.

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

J07 and D10 require the V15 frame graph and main/render ownership handoff. E09
verifies the V16 physics batches against the serial result oracle. Basic extraction
remains sequential until these integration slices are ready.

## Implementation backlog

**A01 - Documentar o comportamento de referência.** Depende: nenhum.
Arquivos (2): `README.md`, `docs/legacy-game.md`.
Entrega: reproduzir build e execução existentes, registrar controles, modos,
seed, regras do PDF e diferenças conhecidas. Separar bug de comportamento desejado.
Aceite: um desenvolvedor identifica como iniciar e encerrar os três modos; não
declarar bugs corrigidos ou determinismo sem evidência.

**A02 - Higiene de arquivos pessoais e gerados, primeira parte.** Depende: A01.
Arquivos (até 7): `.gitignore`, os 3 `CMakeLists.txt.user*`, os 3 arquivos de query
em `.cmake/api/v1/query/`.
Entrega: remover esses arquivos do índice do projeto e ignorar builds/IDE locais;
preservar o PDF e configurações compartilháveis. Aceite: novos builds não sujam Git.

**A03 - Higiene do cache antigo de CMake.** Depende: A02.
Arquivos (5): os 5 caminhos atualmente versionados em `CMakeFiles/`.
Entrega: retirar metadados gerados do repositório. Aceite: checkout limpo pode ser
configurado sem os caminhos antigos de Qt/MinGW e sem apagar arquivos do usuário.

**A04 - Corrigir build e adotar C++20.** Depende: A01, Q01.
Arquivos (4): `CMakeLists.txt`, `CMakePresets.json`, `CMakeSettings.json`, `README.md`.
Entrega: flags por target/compilador, Debug/Release reais, C++20 explícito e builds
fora da árvore de fontes. Aceite: configurar e compilar nas duas configurações;
registrar toolchain suportada e remover a configuração de IDE redundante.

**A05 - Introduzir testes e job obrigatório mínimo.** Depende: A04.
Arquivos (5): `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/core/vector_tests.cpp`,
`.github/workflows/ci.yml`, `docs/development.md`.
Entrega: CTest com teste útil de coordenadas; build Windows e agregador de status.
Aceite: checkout limpo compila/testa em CI; falha de compilação bloqueia o gate.

**A06 - Tornar includes e unidades de compilação explícitos.** Depende: A05.
Status: merged through PR #28 (`aa21274`). Paths (10): `renderer.cpp`, `renderer.h`, `PowerUp.cpp`,
`PlayField.cpp`, `GameObject.h`, `GameMenus.h`, `tests/CMakeLists.txt`,
`tests/core/header_tests.cpp`, this plan and the task diagram.
Entrega: incluir a própria declaração primeiro e declarar dependências reais.
Aceite: headers públicos compilam isoladamente sem unity build/PCH esconder erros;
não introduzir includes de `.cpp`. Se aparecer ciclo estrutural, dividir por header.

A06 delivery audit: the three legacy implementation files now include their own
declaration first. Renderer exposes only a PlayField forward declaration; its
implementation includes the actual game mode/world, Win32 and stream facilities.
GameMenus explicitly includes its list/pair/vector/Win32 dependencies; GameObject
includes utility for std::move. No runtime behavior or resource strings were added.
The existing formatting standard is applied to touched source files.

All 16 headers registered in the legacy game's `project_headers` compile in
independent object targets, each included twice to check guards. Targets explicitly
disable unity and precompiled headers. Their normal ALL build participates in the
existing native CI job; CTest runtime counts do not include these compile checks.
Actual MSVC C++20 Debug/Release builds compile/link SpaceRaiders and pass 33 existing
GoogleTest/CTest cases each. Compile commands were inspected for separate sources
and absent PCH/forced-prefix inputs. No mock headers or SDK stubs are used.

Known limits: this proves legacy header/build integration, not full gameplay,
engine extraction, minimum transitive include graphs or portability. Full manual
playthroughs were not performed; A07-A11 retain ownership/time/gameplay fixes.
Warnings C4100/C4244/C4267 remain in legacy parameters/conversions, and O04 already
owns target warning/static-analysis adoption in small batches. These are not
silently described as resolved. No additional in-scope A06 acceptance gap was found.

**A07 - Corrigir ownership e filas de remoção.** Depende: A06.
Status: merged through PR #29 (`9b75b30`). Paths (10): `PlayField.h`, `PlayField.cpp`, `renderer.h`,
`renderer.cpp`, `SpaceRaiders.cpp`, `Alien.h`, `tests/CMakeLists.txt`,
`tests/runtime/legacy_lifetime_tests.cpp`, this plan and the task diagram.
Entrega: ownership único, limpeza de filas/objetos/modo e proibição de cópia de
donos de recursos. Remoção é idempotente; objetos removidos não pontuam duas vezes.
Aceite: destruir mundo com filas pendentes, trocar modo e repetir remoção é seguro.

A07 delivery audit: active/pending entities, game mode, controller and renderer
buffers now have unique ownership. World/renderer copying and moving are explicitly
disabled. Mode replacement releases the previous instance; reinstalling the same
mode is a no-op and clearing it is supported. The real game entry point assigns
owned input. Destruction/RemoveAllObjects drain both object queues and removal
markers; flushing an emptied world cannot resurrect old pending spawns.

Removal is idempotent for owned active/pending objects; null/foreign removal is
ignored. Objects stay allocated until the queue boundary but disappear immediately
from collision traversal, updates and player/alien queries. Duplicate registration
cannot create a second owner or spend a second laser slot. Repeated despawn returns
at most one slot. Alien death awards score/drops once; two actual player lasers
cannot score the same removed alien twice. Borrowed object pointers/views must not
outlive queue processing or clearing. These legacy APIs remain single-threaded;
queue flushing/clearing is an outer-loop operation, not an entity update callback.

Validation: MSVC C++20 Debug/Release builds compile/link the actual game, all 16
isolated header targets and 50 GoogleTest/CTest cases each (17 new lifetime cases).
New tests link the production world, renderer and power-up translation units.
Real player/alien/laser/mode classes exercise collision scoring, immediate removal
queries, wave creation, pending teardown, and scene ticks/restart using the real
RndInput implementation. Instrumented test objects/modes/inputs separately count
updates and exactly-once destruction; those counters are fixture evidence rather
than real gameplay. The 17 lifetime cases also pass MSVC AddressSanitizer.
The existing CI native job builds/discovers them through CTest without a new job.

Limits: no full interactive playthrough, allocation-failure injection, leak-profiler
capture or performance benchmark was performed. AddressSanitizer is a local memory
access check, not a Windows leak detector or a new CI job. No external SDK adapters,
manifests or asset fixtures are required by this legacy ownership slice. No known
in-scope A07 acceptance gap remains. Timing (A08), game-over/contact rules (A09),
full laser budgets/collision effects (A10), buffs/drop balancing (A11), and existing
legacy warnings (O04) remain explicitly planned. Clearing releases entities but
does not reset gameplay slot policy; A10 owns that reset/reconciliation contract.

**A08 - Separar tempo simulado de pacing legado.** Depende: A07.
Status: implemented and locally validated on `A08_fixed_timestep_and_frame_pacing`,
pending review/merge. Paths (7): `VSync.h`, `SpaceRaiders.cpp`, `GameMode.h`,
`tests/CMakeLists.txt`, `tests/runtime/legacy_time_tests.cpp`, this plan and the
task diagram.
Entrega: relógio monotônico, timestep fixo e sleep limitado a valor não negativo.
Aceite: ticks equivalentes independem da demora do renderer; time attack termina
no tick previsto; atraso grande possui limite explícito de recuperação.

A08 delivery audit: the actual game loop now samples std::chrono::steady_clock,
runs zero or more fixed simulation ticks, processes object queues per tick and
renders once per batch. Every existing movement/lifetime consumer of DeltaTime
receives the same 16 ms step; rendering speed no longer selects a gameplay delta.
This deliberately preserves the legacy 16 ms base (62.5 ticks/s, not exact 60 Hz).
The accumulator retains fractional time. Each Advance accepts at most 80 ms of
elapsed time (five ticks); extra stall time is discarded, preserving any earlier
fractional remainder. Simulation can therefore lag wall time after a long stall,
without an unlimited catch-up burst/backlog. Reset excludes menu/setup time.

The sleep duration includes work already spent updating/rendering, saturates at
zero and does not mutate simulation time. Oversleep is measured on the next frame.
Backward injected timestamps are ignored without moving the last valid anchor.
TimeAttack consumes fixed integer milliseconds, saturates at zero and ends on
tick 3,688 for the existing 59-second duration. Catch-up stops on game over; no
additional ticks or sleep run after that terminal update. Whole-second HUD display
still truncates, so it can show zero during the last fractional second.

Validation: Windows x64 MSVC C++20 Debug/Release build/link of the game and all
16 independent header checks, plus 64 GoogleTest/CTest cases in each configuration
(14 new timing cases). Controlled timestamp/input fixtures exercise exact boundaries,
sub-millisecond remainders, equal movement/timer results under different schedules,
irregular frame delays, reset, regressing input, long stalls and terminal batches.
The movement/timer cases use the real PlayField, PlayerShip and TimeAttack classes;
only time/input are controlled. A separate local smoke case runs the actual native
renderer, monotonic clock and sleep, captures console output and checks bounded
pacing/tick counts. It does not assert Windows scheduling precision or performance.
CTest registration uses the existing CI native job; no new pipeline is required.

Limits: no full interactive keyboard playthrough, rendering interpolation,
performance benchmark, wall-clock determinism across machines or new engine clock
abstraction is claimed. Rendering and simulation still share the main thread;
future application-loop/thread extraction remains B06/B07/V15. A09 retains final-scene,
contact and post-game wave priority rules; A10/A11 retain collision/buff work.
Legacy warnings remain tracked by O04. No additional adapters/manifests are needed
for this native slice and no known in-scope A08 acceptance gap remains.

**A09 - Corrigir alien/player e transições de fim de jogo.** Depende: A08.
Arquivos (7): `Alien.h`, `PlayerShip.h`, `GameMode.h`, `PlayField.cpp`,
`SpaceRaiders.cpp`, `tests/CMakeLists.txt`, `tests/game/legacy_rules_tests.cpp`.
Entrega: limites, morte ao alien chegar ao fundo, destruição de ambos em contato,
congelamento da cena final e prioridade das condições de término/novas ondas.
Aceite: nenhum modo repovoa uma partida encerrada; posição final permanece visível.

**A10 - Corrigir colisões e orçamento de lasers.** Depende: A09.
Arquivos (7): `PlayerLaser.h`, `PlayerDiagonalLaser.h`, `AlienLaser.h`,
`PlayField.h`, `PlayField.cpp`, `BlockWall.h`, `tests/game/legacy_rules_tests.cpp`.
Entrega: slots coerentes para tiro triplo, laser contra laser, uma resolução por
contato e deslocamento de parede sem recursão; reset/reconciliação de slots ao
limpar/reiniciar o mundo. Aceite: slots retornam ao despawn,
uma colisão não duplica score/efeito, parede sobreposta não trava a atualização.

**A11 - Corrigir evolução e buffs temporários.** Depende: A10.
Arquivos (6): `Alien.h`, `PlayerShip.h`, `PowerUp.h`, `PowerUp.cpp`, `Input.h`,
`tests/game/legacy_rules_tests.cpp`.
Entrega: chance de transformação depois da energia, cadência de alien melhorado,
expiração/renovação de buffs, pesos de drop explícitos e cast verificado.
Aceite: testar fronteiras com RNG controlado; buff expira; mesma sequência de
inputs/seed produz mesmos eventos no mesmo build. Não depender de sequência RNG
idêntica entre bibliotecas padrão de compiladores diferentes.

**B01 - Criar o núcleo e mover o vetor.** Depende: A11.
Arquivos (até 6): `CMakeLists.txt`, `engine/core/CMakeLists.txt`,
`engine/core/include/raiders/Vector2D.h` (rename), `Vector2D.h` (shim temporário),
`GameObject.h`, `tests/core/vector_tests.cpp`.
Entrega: biblioteca `raiders_core` independente do jogo. Aceite: compila sem Win32
ou headers de gameplay; shim limita a movimentação aos consumidores necessários.

**B02 - Definir serviços de plataforma e input.** Depende: B01.
Arquivos (7): `engine/core/include/raiders/{Platform,InputSnapshot}.h`,
`engine/platform/console/{CMakeLists.txt,ConsoleInput.h,ConsoleInput.cpp}`,
`Input.h`, `CMakeLists.txt`.
Entrega: snapshot de teclas/eventos e adapter legado Win32. Aceite: consultar input
duas vezes no tick não sorteia estados diferentes; Win32 fica no backend.

**B03 - Extrair comandos de renderização.** Depende: B02.
Arquivos (6): `engine/render/{CMakeLists.txt,include/raiders/RenderCommand.h}`,
`renderer.h`, `renderer.cpp`, `SpaceRaiders.cpp`, `CMakeLists.txt`.
Entrega: comandos sem `GameMode`/`PlayField` e transporte explícito do HUD legado.
Aceite: renderer recebe somente dados de apresentação, sem consultar score no mundo.

**B04 - Isolar o backend de console.** Depende: B03.
Arquivos (até 8): `renderer.h/.cpp` movidos para
`engine/platform/console/{ConsoleRenderer.h,ConsoleRenderer.cpp}`,
`engine/platform/console/CMakeLists.txt`, `SpaceRaiders.cpp`, `GameMenus.h`,
`PowerUp.h`, `CMakeLists.txt`.
Entrega: buffers com RAII e saída Win32 encapsulada; símbolos de jogo ficam fora
da API genérica. Aceite: jogo continua desenhando e pode trocar por renderer fake.

**B05 - Separar descritores visuais do jogo.** Depende: B04.
Arquivos (até 9): `game/include/raiders_game/Sprites.h`, `Alien.h`,
`AlienLaser.h`, `BlockWall.h`, `Explosion.h`, `PlayerShip.h`, `PlayerLaser.h`,
`PlayerDiagonalLaser.h`, `PowerUp.h`.
Entrega: nomes dos sprites pertencem ao jogo. Aceite: engine não referencia enums
de aliens, power-ups ou jogador; todas as aparências atuais são preservadas.

**B06 - Extrair Application e loop fixo.** Depende: B04.
Arquivos (8): `engine/runtime/{CMakeLists.txt,include/raiders/Application.h,
include/raiders/RunLoop.h,src/RunLoop.cpp}`, `SpaceRaiders.cpp`,
`CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/runtime/run_loop_tests.cpp`.
Entrega: engine controla pump, ordem dos hooks e shutdown; jogo implementa hooks.
Aceite: fake clock/input verificam inicialização, passos, pausa, encerramento e
erro de inicialização sem loop de menu bloqueante.

**B07 - Colocar o entry point na engine.** Depende: B06.
Arquivos (7): `engine/runtime/src/EntryPoint.cpp`, `engine/runtime/CMakeLists.txt`,
`game/src/SpaceRaidersApplication.cpp`, `SpaceRaiders.cpp`, `game/CMakeLists.txt`,
`CMakeLists.txt`, `tests/runtime/sample_application.cpp`.
Entrega: `engine_entry`, biblioteca `raiders_game`, factory de Application e target
executável de composição. Aceite: só a engine define `main`; aplicação de teste
linka sem código do jogo; erros da factory produzem saída não zero.

**B08 - Mover mundo e modos de jogo.** Depende: B07.
Arquivos (até 9): renames de `PlayField.h/.cpp`, `GameMode.h`, `GameMenus.h` para
`game/`, `game/CMakeLists.txt` e até 4 consumidores.
Entrega: agrupamento físico mantendo adapters de include se necessário.
Aceite: build preservado; se houver mais consumidores, fazer B08a/B08b.

**B09 - Mover entidades de jogador e projéteis.** Depende: B08.
Arquivos (até 8): renames de `PlayerShip.h`, `PlayerLaser.h`,
`PlayerDiagonalLaser.h`, `AlienLaser.h`, `game/CMakeLists.txt` e até 3 consumidores.
Aceite: paths públicos do jogo previsíveis; sem mover outros subsistemas junto.

**B10 - Mover entidades restantes.** Depende: B09.
Arquivos (até 9): renames de `Alien.h`, `BlockWall.h`, `Explosion.h`, `PowerUp.h/.cpp`,
`game/CMakeLists.txt` e até 3 consumidores.
Aceite: mesmos testes e execução; não misturar alterações de regras nos renames.

**B11 - Remover shims e fechar a fronteira das bibliotecas.** Depende: B10, B05.
Arquivos (até 8): `Vector2D.h`, `GameObject.h`, `Input.h`, `VSync.h`,
`CMakeLists.txt`, `game/CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/integration/engine_boundary_tests.cmake`.
Entrega: mover utilitários restantes ou removê-los após adaptar consumidores em
sub-PRs se preciso. Aceite: engine compila sozinha; não há includes de gameplay
na engine; ninguém usa caminhos antigos. Não concentrar correções inesperadas aqui.

**C01 - IDs e armazenamento de entidades.** Depende: B07.
Arquivos (5): `engine/runtime/include/raiders/{EntityId,World}.h`,
`engine/runtime/src/World.cpp`, `engine/runtime/CMakeLists.txt`,
`tests/runtime/world_tests.cpp`.
Entrega: handles com geração e armazenamento dono. Aceite: handle velho não acessa
uma entidade nova que reutilizou o slot; destruição duplicada é segura.

**C02 - Componentes básicos por composição.** Depende: C01.
Arquivos (6): `engine/runtime/include/raiders/{ComponentStore,Transform2D,Sprite}.h`,
`engine/runtime/src/ComponentStore.cpp`, `engine/runtime/CMakeLists.txt`,
`tests/runtime/component_tests.cpp`.
Entrega: adicionar/remover/consultar componentes sem derivar um C++ por personagem.
Aceite: componentes são destruídos com a entidade e ausência é tratada explicitamente.

**C03 - Hierarquia e comandos diferidos.** Depende: C02.
Arquivos (5): `engine/runtime/include/raiders/World.h`,
`engine/runtime/src/World.cpp`, `engine/runtime/include/raiders/Transform2D.h`,
`tests/runtime/world_tests.cpp`, `tests/runtime/component_tests.cpp`.
Entrega: parent/child sem ciclos e filas aplicadas em pontos definidos.
Aceite: callback pode solicitar spawn/despawn sem invalidar iteração; definir
destruição em cascata e transform local/mundo.

**C04 - RNG e eventos por mundo.** Depende: C03.
Arquivos (6): `engine/runtime/include/raiders/{Random,WorldEvents}.h`,
`engine/runtime/src/{Random,WorldEvents}.cpp`, `engine/runtime/CMakeLists.txt`,
`tests/runtime/random_events_tests.cpp`.
Entrega: seed/estado injetáveis e ordem estável de eventos. Aceite: replay de
inputs/seed/ticks reproduz eventos no ambiente suportado, sem RNG global.

**C05 - Migração vertical do jogador legado.** Depende: C04, B11.
Arquivos (até 8): mundo e aplicação em `game/`, componente de jogador,
adapter legado, `game/CMakeLists.txt` e teste de integração do jogador.
Entrega: um jogador real usa World/Transform/Sprite; adapter impede duplicar estado
de posição/vida. Aceite: mover, atirar e destruir continuam corretos; listar paths
exatos antes de implementar e subdividir se a adaptação exigir mais de 8.

**D01 - Stable configurable draw ordering and clipping.** Depends: C02, B04.
Paths (6): `engine/render/include/raiders/{RenderCommand,RenderQueue}.h`,
`engine/render/src/RenderQueue.cpp`, `engine/render/CMakeLists.txt`,
`engine/platform/console/ConsoleRenderer.cpp`, `tests/core/render_queue_tests.cpp`.
Acceptance: stable layer ID/order/sort key and ties; UI selects a layer like other
content, with defaults but no reserved mandatory frontmost UI pass. Hit testing
uses compatible visual order; cross-layer clipping behavior is explicit.

**D02 - Sprites de múltiplas células.** Depende: D01.
Arquivos (5): `engine/render/include/raiders/GlyphSprite.h`,
`engine/render/src/GlyphSprite.cpp`, `engine/render/CMakeLists.txt`,
`engine/platform/console/ConsoleRenderer.cpp`, `tests/core/glyph_sprite_tests.cpp`.
Entrega: grade, origem, célula transparente e cor com fallback definido.
Aceite: sprite parcialmente fora da tela não escreve fora do buffer.

**D03 - Extração de desenho do mundo.** Depende: D02, C03.
Arquivos (5): `engine/render/include/raiders/WorldRenderer.h`,
`engine/render/src/WorldRenderer.cpp`, `engine/render/CMakeLists.txt`,
aplicação em `game/`, `tests/integration/world_render_tests.cpp`.
Aceite: Transform/Sprite produzem comandos; renderer não muda componentes.

**E01 - Corpos e integração.** Depende: C03.
Arquivos (6): `engine/physics/{CMakeLists.txt,include/raiders/RigidBody2D.h,
include/raiders/PhysicsWorld.h,src/PhysicsWorld.cpp}`, `CMakeLists.txt`,
`tests/physics/integration_tests.cpp`.
Aceite: força/massa geram aceleração; sem força e sem damping mantém inércia;
corpos estáticos não se movem; timestep é o do loop fixo.

**E02 - Basic collider/filter contract.** Depends: E01.
Paths (5): `engine/physics/include/raiders/{Collider2D,CollisionFilter}.h`,
`engine/physics/src/PhysicsWorld.cpp`, `engine/physics/CMakeLists.txt`,
`tests/physics/collider_tests.cpp`.
Acceptance: circle/AABB foundation, masks/sensors and local transforms. The planned
shape family also includes rotated rectangle/square, triangle and 2D capsule (E05);
no custom polygon/mesh collider input.

**E03 - Eventos e resposta de colisão.** Depende: E02, C04.
Arquivos (5): `engine/physics/include/raiders/{Contact,PhysicsWorld}.h`,
`engine/physics/src/PhysicsWorld.cpp`, `engine/runtime/include/raiders/WorldEvents.h`,
`tests/physics/contact_tests.cpp`.
Aceite: begin/end sem duplicações; sensores não empurram corpos; destruir entidade
em callback não deixa contato pendurado; penetração é resolvida em ordem definida.

**E04 - Consulta contínua para projéteis.** Depende: E03.
Arquivos (4): `engine/physics/include/raiders/PhysicsWorld.h`,
`engine/physics/src/PhysicsWorld.cpp`, `tests/physics/projectile_tests.cpp`,
`docs/physics.md`.
Aceite: projétil que cruza alvo entre ticks acerta o primeiro contato; empate,
spawn sobreposto e limite de consultas têm tratamento explícito.

**H01 - Árvore de widgets e layout básico.** Depende: F04, D01.
Arquivos (6): `engine/ui/{CMakeLists.txt,include/raiders/Widget.h,
include/raiders/UiLayout.h,src/UiLayout.cpp}`, `CMakeLists.txt`,
`tests/ui/layout_tests.cpp`.
Entrega: canvas, painel e texto; parent, anchors, tamanho, visibilidade e clipping.
Aceite: layout tem resultado previsível em resoluções diferentes e sem ciclos.

**H02 - Desenho de UI e estilos.** Depende: H01, D02.
Arquivos (5): `engine/ui/{include/raiders/UiRenderer.h,src/UiRenderer.cpp,
include/raiders/UiStyle.h}`, `engine/ui/CMakeLists.txt`,
`tests/ui/render_tests.cpp`.
Aceite: UI gera comandos na layer correta, respeita clipping e não acessa modos
ou objetos específicos do jogo. Texto muito longo tem política de overflow.

**H03 - Input de ponteiro e hit testing.** Depende: H02, B02.
Arquivos (5): `engine/core/include/raiders/InputSnapshot.h`,
`engine/platform/console/ConsoleInput.cpp`, `engine/ui/include/raiders/UiInput.h`,
`engine/ui/src/UiInput.cpp`, `tests/ui/hit_test_tests.cpp`.
Entrega: eventos de mouse da console, transformação tela/canvas e ordem de hits.
Aceite: widget superior recebe evento; invisível/desabilitado não captura input.

**H04 - Botões, foco e navegação.** Depende: H03.
Arquivos (6): `engine/ui/include/raiders/{Button,UiFocus}.h`,
`engine/ui/src/{Button,UiFocus}.cpp`, `engine/ui/CMakeLists.txt`,
`tests/ui/focus_tests.cpp`.
Aceite: hover/press/release/cancel, mouse capture, Tab/Enter e perda de foco
funcionam; clicar na UI não dispara também um tiro no gameplay.

**H05 - Binding Lua de widgets e eventos.** Depende: H04, G07.
Arquivos (6): `engine/scripting/{include/raiders/UiBindings.h,src/UiBindings.cpp}`,
`engine/scripting/CMakeLists.txt`, `engine/ui/src/Button.cpp`,
`tests/scripting/ui_bindings_tests.cpp`, `tests/fixtures/ui_script.lua`.
Entrega: referências por ID, propriedades permitidas e handlers.
Aceite: descarregar tela invalida handles e listeners; script UI não obtém ponteiro
de widget nem World de gameplay.

**H06 - Animações simples de propriedades.** Depende: H05.
Arquivos (5): `engine/ui/{include/raiders/UiTween.h,src/UiTween.cpp}`,
`engine/ui/CMakeLists.txt`, `engine/scripting/src/UiBindings.cpp`,
`tests/ui/tween_tests.cpp`.
Entrega: duração/easing para escala, posição e cor; política de substituição/cancelamento.
Aceite: botão reduz ou muda cor no clique, restaura estado e cancela tween ao destruir.

**H07 - Instanciar UI a partir do modelo intermediário.** Depende: H06, F05.
Arquivos (5): `engine/ui/{include/raiders/UiFactory.h,src/UiFactory.cpp}`,
`engine/ui/CMakeLists.txt`, `tests/ui/factory_tests.cpp`,
`tests/fixtures/ui_factory.xml`.
Aceite: IDs, referências Lua e propriedades importadas correspondem à UI criada;
the UI factory receives typed descriptors; Development supplies them through shared
XML deserialization, Shipping through binary/compiled data. The factory itself
does not own parsing, but XML loading is supported by the development host.

**J07 - Native engine preview host with real renderer.** Depends: J06, G09, R09, D05, V15.
Paths (6): `apps/preview_host/{CMakeLists.txt,PreviewApplication.cpp,
PreviewProtocol.h,PreviewProtocol.cpp}`, `CMakeLists.txt`,
`tests/integration/preview_host_tests.cpp`.
Acceptance: same renderer/backend, shaders if ever added, assets, camera, layers,
animation clock and render settings as standalone. Native offscreen surface or
native hosted window is exposed to C#; no scene re-rasterization in C#.
Editing loads serialized source-bundle XML/Lua through the development source
provider, without cooked files or content generation. Run/pause/reset and process
failure preserve editor documents; binary/compiled loading is a separate package path.


## Rendering and physics extensions

The final sample is 2D, with rotated drawings and image sprites; a console-only
renderer is not sufficient for that fidelity. Keep console as a legacy adapter,
then choose a native window/offscreen presentation backend in D04. A simple native
software rasterizer is a viable initial implementation; do not require a custom
shader/material/lighting system. Exactly one native scene rasterization path serves
game, entity/UI editor previews and level playtest. C# presents completed pixels
or hosts the native window; selection handles are editor overlays, excluded from
render equivalence tests. Match viewport, DPI scale, font resources, color settings,
camera and simulation time before comparing frames.

Drawings are authored 2D primitives/strokes and may animate or rotate. They are
independent of collision shapes: drawing a triangle does not create an arbitrary
mesh collider. Sprite-frame animation, transform tracks and Lua events are sufficient
initially; no skeletal animation system or general-purpose cinematic editor.

Layers are data: stable IDs and authored order, with per-item sorting and a stable
tie-break. UI can be assigned any valid layer. Avoid an unconditional final UI pass
that defeats interleaving; focus/hit testing obey final visual order and input policy.

Physics is fixed-step, with linear/angular motion and simple shape mathematics.
Quadtree is the first spatial-partition candidate, not a replacement for narrow
phase or CCD. Test its candidate pairs against a brute-force oracle; retain a
fallback/threshold for dense scenes and profile update cost. Toroidal boundaries
need seam candidates/image transforms or an explicit collision rule; merely
teleporting centers misses contacts at opposite edges.

**D04 - Native offscreen surface and backend decision.** Depends: D03, T03.
Paths (5): `engine/render/{include/raiders/RenderSurface.h,src/RenderSurface.cpp}`,
`engine/render/CMakeLists.txt`, `tests/render/surface_tests.cpp`,
`docs/decisions/0003-render-surface.md`.
Acceptance: native pixel/surface contract and Windows presentation backend chosen;
document console limitations and RGBA/stride/alpha/color/DPI conventions.

**D05 - Standalone native window presentation.** Depends: D04.
Paths (6): `engine/platform/window/{CMakeLists.txt,Window.h,Window.cpp}`,
`CMakeLists.txt`, `game/src/SpaceRaidersApplication.cpp`,
`tests/render/window_present_tests.cpp`.
Acceptance: game presents RenderSurface without scene redraw; resize/input/focus
work; no GPU/shader stack is required for the initial 2D renderer.

**D06 - Image sprites and resource lifecycle.** Depends: D04, F07.
Paths (6): `engine/render/{include/raiders/ImageSprite.h,src/ImageSprite.cpp}`,
`engine/render/CMakeLists.txt`, `tools/authoring/src/SpriteXml.cpp`,
`tests/render/image_sprite_tests.cpp`, `tests/fixtures/sprite.png`.
Acceptance: decode/stride/transparency/origin/rotation validated; allocation tags
report decoded/retained bytes; atlas/subregion references retain the owner correctly.

**D07 - Native authored drawing primitives.** Depends: D04.
Paths (5): `engine/render/{include/raiders/Drawing2D.h,src/Drawing2D.cpp}`,
`engine/render/CMakeLists.txt`, `tests/render/drawing_tests.cpp`,
`schemas/drawing.schema.json`.
Acceptance: pen strokes/lines and filled triangles/rectangles/circles rasterize
with local transforms and clipping; render shapes do not imply collider meshes.

**D08 - Entity animation runtime.** Depends: D06, D07, C03.
Paths (6): `engine/animation/{CMakeLists.txt,include/raiders/AnimationClip.h,
include/raiders/Animator.h,src/Animator.cpp}`, `CMakeLists.txt`,
`tests/animation/animation_tests.cpp`.
Acceptance: sprite frames, transform/property tracks, loop/one-shot, timing/events,
pause/reset; same clock/sampling at game and preview; destroyed targets cancel tracks.

**D09 - Layer registry across schemas/runtime.** Depends: D01, F04, F08.
Paths (6): `engine/render/include/raiders/RenderLayer.h`,
`engine/render/src/RenderQueue.cpp`, `tools/authoring/{src/UiXml.cpp,src/SceneXml.cpp}`,
`tests/render/layer_registry_tests.cpp`, `docs/rendering.md`.
Acceptance: stable named layer IDs and authored order; UI layer selectable;
unknown/removed layer references fail validation rather than silently moving to front.

**D10 - Renderer equivalence tests.** Depends: D05, J07, D08, D09, V15.
Paths (5): `tests/integration/render_parity_tests.cpp`,
`tests/fixtures/render_scene.bundle/manifest.json`, `tests/CMakeLists.txt`,
`apps/preview_host/PreviewProtocol.cpp`, `docs/rendering.md`.
Acceptance: game/preview use the same native frame code and produce identical
offscreen pixels for same inputs at fixed viewport/time. Include UI, primitive,
rotated sprite, animation and non-front UI layer; C# transport must preserve pixels.

**E05 - Rotated simple-shape narrow phase.** Depends: E02.
Paths (6): `engine/physics/{include/raiders/Shapes2D.h,src/ShapeContacts.cpp,
src/PhysicsWorld.cpp}`, `engine/physics/CMakeLists.txt`,
`tests/physics/shape_matrix_tests.cpp`, `docs/physics.md`.
Acceptance: circles, oriented squares/rectangles, triangles, 2D capsules and every pair combination within this shape family; local offset/rotation, degeneracy and contact tolerances tested;
no user-authored arbitrary polygon/mesh. Split shape pairs into sub-PRs if needed.

**E06 - Angular integration and contact response.** Depends: E05, E03.
Paths (5): `engine/physics/include/raiders/RigidBody2D.h`,
`engine/physics/src/{PhysicsWorld,ShapeContacts}.cpp`,
`tests/physics/angular_tests.cpp`, `docs/physics.md`.
Acceptance: rotation/angular velocity/torque and simple moments of inertia;
static bodies and positional correction stable; gameplay defaults have zero gravity.

**E07 - Quadtree broad phase with correctness oracle.** Depends: E05, T03.
Paths (5): `engine/physics/{include/raiders/Quadtree.h,src/Quadtree.cpp}`,
`engine/physics/CMakeLists.txt`, `tests/physics/quadtree_tests.cpp`,
`tests/physics/broadphase_scenarios.cpp`.
Acceptance: dynamic rotated AABBs, large/root-crossing objects, deduplicated pairs,
depth/capacity limits and dense fallback; no missed pair versus brute force;
measure maintenance cost before claiming an optimization.

**E08 - Toroidal screen-boundary queries.** Depends: E06, E07, E04.
Paths (5): `engine/physics/{include/raiders/BoundaryPolicy.h,src/BoundaryPolicy.cpp,
src/PhysicsWorld.cpp}`, `engine/physics/CMakeLists.txt`,
`tests/physics/wrap_contact_tests.cpp`.
Acceptance: top/bottom and left/right wrapping retain position remainder,
orientation/velocity; seam contact/CCD works for rotated shapes without duplicate
entity IDs, damage or fragmentation; interpolation does not streak across screen.

**E09 - Integrated physics/animation performance scenarios.** Depends: E08, D08, T09, V16.
Paths (4): `tests/physics/physics_scenarios.cpp`,
`tests/animation/spawn_contact_tests.cpp`, `tests/CMakeLists.txt`,
`docs/physics.md`.
Acceptance: bounded spawn populations, quadtree candidate counts and memory,
fast-projectile collisions, wrap corners and animation-controlled sensor/enabled
states; no frame-rate-dependent behavior.


## Legacy baseline: diagnóstico da base real

Foram lidos todos os fontes de aplicação (`.h` e `.cpp` na raiz), os arquivos de
configuração de build, as configurações de GitHub e as duas páginas de
`IKACppTest.pdf`. Os diretórios `.cmake/` e `CMakeFiles/` contêm metadados gerados,
inclusive referências a um MinGW e a caminhos de uma instalação antiga de Qt.
Não são implementação da engine.

Não foi executado o jogo nem comprovado um build nesta atividade de planejamento.
O usuário informa que o jogo compila e roda. O primeiro passo de implementação
reproduzirá esse resultado com uma configuração limpa; os problemas abaixo são
achados de leitura, não resultados de testes executados.

### Inventário

| Arquivos | Responsabilidade atual | Problema de separação |
| --- | --- | --- |
| `SpaceRaiders.cpp` | `main`, seed, menus, escolha de modo, criação do mundo, loop e pacing | Plataforma, engine e composição do jogo no mesmo ponto |
| `GameObject.h`, `Vector2D.h` | Objeto polimórfico com posição/símbolo e vetor básico | Herança por tipo de jogo; ainda não há composição de componentes |
| `PlayField.h/.cpp` | Objetos, filas, input, modo, contadores e criação de ondas | Mundo genérico conhece aliens, jogador e paredes |
| `renderer.h/.cpp` | Canvas duplo, símbolos e saída na console Windows | Renderer conhece modos, tempo e placar do jogo |
| `Input.h` | Teclado Win32 e inputs aleatórios | API de plataforma e gerador global chegam a toda a aplicação por includes |
| `VSync.h` | Estimativa de FPS, delta e sleep | Tempo de simulação e apresentação misturados |
| `GameMode.h`, `GameMenus.h` | Três modos e menus desenhados em código | Regras e UI dependem diretamente do mundo e de Win32 |
| `Alien.h`, `AlienLaser.h` | Movimento, evolução, tiros e colisões de aliens | Regras, criação e destruição acopladas |
| `PlayerShip.h`, `PlayerLaser.h`, `PlayerDiagonalLaser.h` | Movimento lateral, tiros e upgrades | Colisões duplicadas; sem componentes ou scripting |
| `BlockWall.h`, `Explosion.h`, `PowerUp.h/.cpp` | Obstáculos, efeitos e três power-ups | Vida útil manual e regras embutidas em headers |
| `CMakeLists.txt`, `CMakeSettings.json` | Executável único em C++17 e perfil de IDE | Flags globais GCC, otimização forçada e configuração de máquina |
| `.github/` | CODEOWNERS e template de PR | Ainda não há CI |

### Diferença entre a lembrança do jogo e este checkout

A base atual se aproxima de Space Invaders: jogador move no eixo X, aliens descem
pelas bordas, tiros sobem/descem, há paredes, ondas e power-ups. Não existem
propulsão/inércia, meteoros fragmentáveis, editor, XML, Lua ou sistema de componentes.
Esses itens entram como desenvolvimento novo. Os conceitos de mundo, objeto,
input e renderer são pontos de partida, mas ainda não constituem camadas isoladas.

Não foi encontrado nenhum `#include` de arquivo `.cpp` nos fontes de aplicação
desta revisão. Há, porém, dependência de includes transitivos: `renderer.cpp`
recebe tipos Win32 pela cadeia `renderer.h -> GameMode.h -> PlayField.h -> Input.h`,
e `PowerUp.cpp` obtém suas declarações através de `PlayerShip.h`. O plano corrige
isso sem assumir que o problema lembrado existe nesta versão.

### Problemas concretos a tratar

1. **Build:** `add_definitions("-Wall -m64 -O3")` mistura flags de compilador,
   arquitetura e otimização global; não há targets de biblioteca, testes ou install.
   `CMakeFiles/`, `.cmake/` e arquivos pessoais `CMakeLists.txt.user*` estão versionados.
2. **Propriedade:** `PlayField` usa ponteiros crus; o destrutor não libera os
   objetos ativos nem os pendentes. `SetGameMode` pode perder o modo anterior.
   Copiar `Renderer` duplicaria ponteiros donos de buffers.
3. **Ciclo de vida:** destruição é adiada, mas um objeto marcado ainda pode atualizar
   ou receber colisões no mesmo frame; pontuação/efeitos podem acontecer repetidamente.
   `RemoveAllObjects` não resolve as filas. `BlockWall::Update` recorre sobre si mesmo.
4. **Tempo:** `VSync::SetFps` testa limiares na ordem que atribui 5 FPS até para
   frames rápidos. O delta não representa um tick estável; o sleep pode ser negativo.
   Seed fixa sozinha não garante repetibilidade com tempo e consumo de input variáveis.
5. **PDF versus regras:** alien chegar ao fundo só mata se ocupar a célula do player;
   colisão alien/player não garante destruição de ambos; lasers não colidem entre si;
   a transformação acontece imediatamente após atingir energia, sem segunda chance
   aleatória; alien melhorado não ganha cadência própria; game over remove o mundo
   em vez de congelar a cena. Esses comportamentos divergem do enunciado.
6. **Projéteis e limites:** laser diagonal tem outro nome e escapa da contagem de
   slots de laser do jogador. Movimento do jogador não limita X. Projéteis rápidos
   podem atravessar células sem detectar colisões.
7. **Power-ups:** velocidade e cadência não expiram. Após o drop de aproximadamente
   10%, a seleção usa uma amostra em `[0,10]` com limiares `.3/.6`, gerando uma
   distribuição muito concentrada em triple fire. Isso pede uma política explícita,
   pois o PDF não exige pesos iguais. Há `reinterpret_cast` para acessar o jogador.
8. **Apresentação:** HUD conhece modos por `dynamic_cast`; camadas e ordenação de
   desenho não são explícitas. O menu inicial faz polling contínuo sem espera.
