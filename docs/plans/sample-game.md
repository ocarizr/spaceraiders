# Space Raiders sample game

Status: Planned. Implementation has not started.

## Scope and independent execution

A sample of the reusable engine/toolchain, not the owner of engine architecture.
C++20 application composition plus bundle-authored content and Lua gameplay/UI.
Legacy aliens remain a regression/sample mode. The main sample has a rotating ship
with zero-gravity thrust, inertia, reverse thrust, screen wrapping, aliens,
fragmenting asteroids and spawn animations.
Inputs: engine, LuaJIT, bundles, editors and diagnostics. Exports: playable sample,
content templates and regression scenarios. First slice: one authored ship moves,
rotates and wraps in the exact same renderer during playtest and standalone play.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Implementation backlog

**M01 - Registrar extensões de gameplay.** Depende: I09, G06, C05.
Arquivos (6): `game/include/raiders_game/{GameplayComponents,GameBindings}.h`,
`game/src/{GameplayComponents,GameBindings}.cpp`, `game/CMakeLists.txt`,
`tests/game/game_registry_tests.cpp`.
Entrega: Health, Weapon, score e eventos registrados pelo jogo; bindings públicos
para UI. Aceite: engine não precisa conhecer tipos dessas regras.

**M02 - Jogador legado e projétil autorados.** Depende: M01, L03.
Arquivos (até 8): `assets/gameplay.bundle/entities/{legacy_player,player_laser}.xml`,
`assets/gameplay.bundle/scripts/{legacy_player,projectile}.lua`, aplicação e adapter do
jogo, `game/CMakeLists.txt`, `tests/game/scripted_player_tests.cpp`.
Aceite: factory cria jogador a partir de asset e script controla input/cadência;
nenhum assembly C++ do jogador permanece no caminho migrado.

**M03 - Aliens e sua evolução em Lua.** Depende: M02.
Arquivos (até 8): `assets/gameplay.bundle/entities/{alien,alien_laser}.xml`,
`assets/gameplay.bundle/scripts/alien.lua`, adapter/spawner em `game/`, header legado de
alien, `game/CMakeLists.txt`, `tests/game/scripted_alien_tests.cpp`.
Aceite: movimento, evolução, dano e pontuação correspondem às regras estabilizadas;
remover classe antiga somente quando não houver consumidor.

**M04 - Paredes, explosões e power-ups por assets.** Depende: M03.
Arquivos (8): `assets/gameplay.bundle/entities/{wall,explosion,powerup}.xml`,
`assets/gameplay.bundle/scripts/{wall,explosion,powerup}.lua`, adapter do jogo,
`tests/game/scripted_effects_tests.cpp`.
Aceite: efeitos expiram, paredes absorvem hits, buffs expiram e drops preservam
política definida; se exclusões legadas excederem orçamento, removê-las em M09.

**M05 - Cena, ondas e modos legados em Lua.** Depende: M04.
Arquivos (6): `assets/gameplay.bundle/scenes/legacy.xml`,
`assets/gameplay.bundle/scripts/{legacy_scene,waves,legacy_modes}.lua`,
aplicação do jogo, `tests/game/scripted_modes_tests.cpp`.
Aceite: uma nave por partida, seed/configuração injetável, ondas só quando cabem
no modo; regra de término não depende do editor nem de classes C++ por personagem.

**M06 - Menu inicial e seleção de modo em UI XML/Lua.** Depende: M05, K05.
Arquivos (5): `assets/ui.bundle/layouts/main_menu.xml`, `assets/ui.bundle/scripts/main_menu.lua`,
aplicação do jogo, `game/CMakeLists.txt`, `tests/game/menu_tests.cpp`.
Aceite: teclas/botões iniciam modos via comando público; sem polling bloqueante
ou referências UI -> detalhes internos do World.

**M07 - HUD, pausa e game over em UI XML/Lua.** Depende: M06.
Arquivos (8): `assets/ui.bundle/layouts/{hud,pause,game_over}.xml`,
`assets/ui.bundle/scripts/{hud,pause,game_over}.lua`, aplicação do jogo,
`tests/game/ui_flow_tests.cpp`.
Aceite: score/tempo por eventos, pausa mantém estado, game over congela cena e
mostra resultado, navegação não dispara input do jogo por baixo da UI.

**M08 - Remover classes antigas de jogador e lasers.** Depende: M07.
Arquivos (6): os 4 headers legados de jogador/lasers em `game/`,
`game/CMakeLists.txt`, `tests/game/scripted_player_tests.cpp`.
Aceite: não há consumidores dessas classes; input/tiro triplo continuam testados
pelo caminho de assets e scripts.

**M09 - Remover classes antigas de aliens e efeitos.** Depende: M08.
Arquivos (7): os 5 fontes/headers legados de alien, parede, explosão e power-ups,
`game/CMakeLists.txt`, `tests/game/scripted_effects_tests.cpp`.
Aceite: nenhuma regra migrada permanece duplicada; score, drops e buffs passam
nos testes de Lua/assets. Não remover scripts que substituem as classes.

**M10 - Remover mundo, modos e menus legados.** Depende: M09.
Arquivos (até 8): os 4 fontes/headers legados de PlayField/GameMode/GameMenus,
adapter final de mundo, `game/CMakeLists.txt`, aplicação do jogo e teste de regressão.
Aceite: só o World da engine possui entidades; ciclo completo dos três modos
legados continua funcionando sem classes antigas nem shims de compatibilidade.

**N01 - Fixed sample rules and controls.** Depends: M01, E04.
Paths (2): `docs/meteor-mode.md`, `docs/legacy-game.md`.
Acceptance: rotating/moving ship, zero gravity, thrust/reverse thrust with inertia,
top-bottom/left-right screen wrap, aliens and fragmenting asteroids, spawn animations.
Specify numerical tuning, score/lives, spawn protection and maximum population.
Rotation/wrap are confirmed requirements, not optional design questions.
The main sample demonstrates reusable tooling; side-scroller support is a reuse
possibility, not another game to implement now.

**N02 - Authored ship with thrust, rotation and wrapping.** Depends: N01, M02, L04, E08.
Paths (5): `assets/gameplay.bundle/entities/ship.xml`,
`assets/gameplay.bundle/sprites/ship.xml`, `assets/gameplay.bundle/scripts/ship.lua`,
`tests/game/thrust_tests.cpp`, `docs/meteor-mode.md`.
Acceptance: force/angular control uses physics, no automatic center lock; reverse
thrust brakes without artificial stop; orientation/velocity survive screen wrap.
Save/reopen in entity editor and standalone/preview show the same appearance.

**N03 - Tiro e colisão no modo de meteoros.** Depende: N02, E04.
Arquivos (5): `assets/gameplay.bundle/entities/ship_projectile.xml`,
`assets/gameplay.bundle/scripts/{ship,projectile}.lua`,
`game/src/GameplayComponents.cpp`, `tests/game/meteor_shooting_tests.cpp`.
Aceite: tiro nasce à frente da nave, respeita cadência/vida útil e não atravessa
meteoro; comportamento de owner/friendly fire está definido.

**N04 - Meteoros por tamanho e fragmentação.** Depende: N03.
Arquivos (8): `assets/gameplay.bundle/entities/{meteor_large,meteor_medium,meteor_small}.xml`,
`assets/gameplay.bundle/sprites/{meteor_large,meteor_medium,meteor_small}.xml`,
`assets/gameplay.bundle/scripts/meteor.lua`, `tests/game/fragmentation_tests.cpp`.
Aceite: grande gera dois médios, médio dois pequenos, pequeno desaparece; ID
destruído não pontua nem fragmenta duas vezes; filhos têm posições/velocidades
definidas e não geram cadeia infinita no mesmo tick.

**N05 - Spawner, cena e ciclo completo.** Depende: N04, M07.
Arquivos (6): `assets/gameplay.bundle/scenes/meteors.xml`,
`assets/gameplay.bundle/scripts/{meteor_spawner,meteor_mode}.lua`,
`assets/ui.bundle/scripts/main_menu.lua`, `assets/ui.bundle/layouts/main_menu.xml`,
`tests/game/meteor_mode_tests.cpp`.
Aceite: spawn evita jogador, limita população total inclusive fragmentos, usa RNG
do mundo e permite iniciar, jogar, morrer/reiniciar e voltar ao menu.


## Sample scope and reusable systems

The earlier rotating-at-center recollection is not an existing implementation in
this checkout. The target sample explicitly allows movement across the whole screen
as well as rotation. With zero gravity and no implicit drag, releasing thrust
retains motion; reverse thrust applies force rather than setting velocity to zero.
Wrap is a finite toroidal arena, not streaming or an infinite world.

Aliens, asteroids and bullets are authored entities with independent scripts.
Spawn effects are ordinary animations/components with explicit contact activation.
Population limits include fragments and pending spawns. Legacy test modes remain
a regression baseline; avoid a long unrelated redesign of the old game before
demonstrating the new authoring pipeline.

**N06 - Aliens in the zero-gravity sample.** Depends: N05, X02, E08.
Paths (6): `assets/gameplay.bundle/entities/space_alien.xml`,
`assets/gameplay.bundle/scripts/{space_alien,space_waves}.lua`,
`assets/gameplay.bundle/scenes/meteors.xml`,
`tests/game/space_alien_tests.cpp`, `assets/gameplay.bundle/manifest.json`.
Acceptance: aliens arrive/spawn, move/fire through the same component/Lua stack,
coexist with asteroids and observe wrap/owner/contact masks; spawning is bounded.

**N07 - Spawn animation and integrated sample acceptance.** Depends: N06, D08, F10, L07, G18.
Paths (6): `assets/gameplay.bundle/animations/spawn.xml`,
`assets/gameplay.bundle/scripts/spawn.lua`,
`assets/gameplay.bundle/entities/ship.xml`,
`tests/game/spawn_animation_tests.cpp`,
`tests/integration/sample_playtest_tests.cpp`, `docs/meteor-mode.md`.
Acceptance: ship/alien/asteroid spawn transition renders in game and native preview,
activation and collision timing are explicit, restart cleans animation/script state;
designer can alter animation through tooling without rebuilding game code.
