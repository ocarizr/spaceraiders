# Space Raiders: plano de desenvolvimento

Data: 24 de outubro de 2026. Base inspecionada: `9d87fe4`.

Este documento descreve a evolução do jogo existente para uma engine 2D reutilizável,
um jogo composto por entidades e scripts, editores e inspetores C# e uma cadeia de
build, validação, cooking e distribuição. É documentação do projeto destinada ao
repositório. Evidências temporárias de execução, controles pessoais de progresso e
instruções de agentes permanecem locais.

## 1. Diagnóstico da base real

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

## 2. Produto e limites de escopo

- Plataforma inicial: Windows, mantendo o renderer de console e a identidade visual
  por caracteres. O desenho de entidades passa a aceitar sprites de várias células.
- O modo de aliens serve de referência de regressão e será preservado. O modo de
  nave com propulsão e meteoros será adicionado separadamente, sem tratar sua ausência
  atual como bug. Não vamos tentar simular propulsão por movimento lateral legado.
- Engine e jogo nativo em C++20; gameplay e UI em Lua. Física, renderização, armazenamento e
  bindings continuam nativos. Não é necessário um ECS de alta complexidade.
- Ferramentas interativas de desenvolvedor em C#: editores de UI/entidades e
  inspetor de data bundles com visualização do grafo de dependências. Compartilham
  bibliotecas .NET; o preview reutiliza um processo nativo da engine.
  Não fazem parte desta entrega um editor de mundo completo, IDE Lua, multiplayer,
  engine 3D, física de corpos complexos ou port para outros sistemas.
- Autoria de layout em XML; comportamento em Lua. Entidades e cenas também terão
  descrição versionada, usando XML dentro de data bundles de autoria.
- Todos os dados persistidos do projeto pertencem a data bundles versionados;
  XML, Lua, sprites, cenas e metadados têm identidade e dependências no manifesto.
  Bundles de autoria são editáveis/diffáveis; bundles cooked são binários.
- Release consome descritores compilados a partir de C++ gerado, sem parser XML.
  Lua continua sendo script, armazenado como registros de script no bundle cooked.
- Ferramentas de pipeline, validação, cooking e geração podem ser C++ ou Python.
  A versão de Python será fixada quando escolhida, sem restrição prévia a 3.10.
- Conteúdo do repositório em inglês: código, comentários, mensagens, ferramentas,
  documentação e PRs. Este plano pode permanecer em português por exceção explícita;
  seus nomes de arquivos futuros e contratos de API continuam em inglês.
- Ferramentas, testes e fixtures úteis a qualquer desenvolvedor são versionados.
  Helpers exclusivos de agente, screenshots de conferência e logs pessoais não são.

São decisões iniciais revisáveis: console como backend do jogo, XML também para
entidades e um único módulo nativo de Lua. C++20, C# para ferramentas interativas e
Lua para scripting são restrições do projeto. Versões e dependências
serão escolhidas e fixadas nas tarefas específicas, verificando documentação oficial,
licença e compatibilidade. O build deve permitir trabalhar em um projeto isolado;
Bazel é uma opção a avaliar, não uma migração aprovada.

## 3. Arquitetura pretendida

### Targets e direção das dependências

```text
SpaceRaiders (executável)
  + engine_entry (objeto com main, implementado pela engine)
  + raiders_game (biblioteca: factory de Application e configuração do jogo)
       -> raiders_runtime -> raiders_core
       -> raiders_lua -> raiders_runtime + Lua
       -> raiders_assets_cooked (C++ gerado; registros e descritores)
  + raiders_console -> contratos de plataforma/render/input

raiders_asset_model (schema/IR comum, sem XML; C++20)
  <- raiders_assets_cooked e runtime
  <- raiders_authoring (importação/exportação XML, apenas ferramentas)
       <- raiders_assetc (validação/cooking/geração de C++)

Raiders.Tools.Contracts / Raiders.Tools.Common (.NET/C#)
  <- Raiders.UIEditor (.NET/C#)
  <- Raiders.EntityEditor (.NET/C#)
  <- Raiders.BundleInspector (.NET/C#, grafo de dependencias)
  -> assetc por CLI + DTO/schema; sem depender da ABI interna de C++
  -> RaidersPreviewHost (processo C++20, protocolo IPC versionado)

data bundles de autoria -> assetc -> C++ gerado + data bundles cooked
                                      -> jogo e preview nativo
```

As setas indicam dependência, não ordem de link literal. A engine nunca inclui
headers do jogo ou referências a player, meteoro, placar ou nomes de assets do jogo.
O backend de console é conectado na composição, sem Win32 nos headers de domínio.
Dependências externas entram por targets; não por flags/includes globais.

`engine_entry` será inicialmente um OBJECT target, para que o `main` da engine
chegue explicitamente ao executável, sem depender da extração de um símbolo de
uma biblioteca estática. A biblioteca do jogo fornece `CreateApplication` por
contrato público; não fornece outro `main`. Um teste com uma segunda aplicação
mínima comprova que o entry point não está amarrado ao Space Raiders.

As aplicações C# têm entry point e event loop próprios de desktop; não herdam a
Application C++ da engine. O host de preview nativo usa essa Application e expõe
um protocolo local de comandos/eventos, com versão, limites e cancelamento. O
frontend C# nunca precisa portar física, renderer ou VM Lua para simular o jogo.

### Build do monorepo e trabalho por projeto

CMake gera o build nativo e permite escolher targets; .NET/MSBuild compila projetos
C# e permite solutions/filtros por ferramenta. Bazel pode substituir a orquestração
por um grafo de targets multilinguagem; não é uma dependência obrigatória de CMake.
Combinar ambos é possível em fronteiras de dependências, mas manter dois grafos
completos equivalentes criaria manutenção desnecessária.

Premissa para iniciar: CMake para C++20; SDK .NET/MSBuild para C#; CLI Python
pequena de uso geral para comandos de monorepo. Avaliar Bazel antes de consolidar
essa orquestração, com um recorte de C++ + C# + geração. Escolher por isolamento
de projetos, IDE/debug, NuGet, incrementalidade, dependências geradas, Windows,
cache e custo de manutenção. Não migrar neste PR de planejamento.

Cada projeto deve ser editável e compilável com suas dependências reais, sem
construir os demais editores. Prever presets nativos, `.csproj`, uma solution .NET
e filtros por ferramenta; CMake não gerará os projetos C# do SDK .NET. O CI invoca
as mesmas entradas usadas por desenvolvedores. Fixar SDKs/dependências em arquivos
compartilhados, sem exigir toolchains gerenciadas para quem só compila a engine.

Referências oficiais consultadas para esta avaliação (24/10/2026):
[targets de CMake](https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-a-project),
[build de projetos .NET](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-build),
[solution filters](https://learn.microsoft.com/en-us/visualstudio/msbuild/solution-filters),
[modelo de build do Bazel](https://bazel.build/about/intro).

### Application e execução

A engine controla inicialização, relógio, pump de eventos, passos fixos, extração
de comandos de desenho, apresentação e shutdown. A aplicação do jogo configura
serviços e cenas e recebe hooks limitados (`OnStart`, `OnFixedUpdate`, `OnStop`).
Input é fotografado uma vez por frame e consumido nos ticks com política definida.
UI roda com relógio de apresentação; física e gameplay com relógio fixo.

Ordem inicial de um tick: comandos pendentes -> snapshot de input -> callbacks Lua
de gameplay -> integração física -> detecção/resolução de contatos -> eventos de
colisão -> destruições/criações diferidas. A renderização lê o estado finalizado;
callbacks não invalidam a iteração corrente. Pausa não acumula backlog de física.

### Entidades, física e rendering

- IDs com geração protegem contra acesso a entidades destruídas. O mundo possui
  entidades e componentes; referências externas usam handles validados.
- Componentes iniciais: Transform2D, Sprite, RigidBody2D, Collider2D, LuaScript e
  dados de gameplay registrados pelo jogo. Parent/child é explícito, sem ciclos.
- Física inicial: força, massa, velocidade, integração em timestep fixo, colisores
  simples e sensores. Contatos possuem IDs estáveis, begin/end e filtros.
- Broad phase simples primeiro, medida antes de otimização. Projéteis precisam de
  consulta de trajetória/sweep, não apenas igualdade de célula no destino.
- Comandos de desenho carregam layer, ordem e desempate estável. Ordem inicial:
  background, gameplay, efeitos e UI. Overlays de autoria ficam no frontend C#;
  preview pode receber comandos de diagnóstico. HUD não é parte do renderer.
- Sprites em grade e coordenadas de tela/mundo têm conversão definida; escala,
  clipping e hit testing dos editores usam a mesma convenção.

### Lua, UI e preservação de código

Lua será integrado por um módulo nativo com API controlada; uma DLL de plugin e
hot reload binário não são necessários inicialmente. A fronteira do módulo terá
versão de API. Gameplay e UI têm contextos separados e acesso por capacidades,
sem ponteiros C++ crus expostos. Exceções/erros Lua viram diagnósticos com asset,
entidade/widget, script, callback e linha; não atravessam destrutores nativos.

Layout XML de UI referencia IDs estáveis de widgets, estilos, scripts e handlers.
O editor gera um cabeçalho Lua com declarações/referências tipadas no nível da API
e um bloco final delimitado para código customizado. Regenerar atualiza apenas a
parte gerada, preservando byte a byte o bloco customizado. Arquivo sem marcadores
válidos não será sobrescrito; a ferramenta informa o conflito. Renomear widgets
preserva IDs; remover widgets produz diagnóstico para bindings obsoletos.

Scripts de UI ficam em `assets/ui.bundle/scripts/`; scripts de gameplay em
`assets/gameplay.bundle/scripts/`. Ambas as árvores são preservadas como namespaces de registros nos bundles para que
`require` e resolução de módulos tenham regras previsíveis. Handlers de UI só
enxergam gameplay por comandos/eventos públicos. Animações simples trabalham com
propriedades permitidas, easing e duração; não são uma ferramenta de timeline geral.

### Assets, cooking e packaging

```text
Authoring bundle (manifest + XML + Lua) -> validation -> versioned IR
  -> generated C++ descriptors/bindings -> compiler -> executable
  -> cooked .rdb (payloads + Lua source + dependency index) -> runtime/inspector
```

Todos os dados autorados são persistidos em data bundles. Premissa inicial: bundle
de autoria é um diretório `*.bundle/` com `manifest.json`, IDs, versões, hashes e
referências. XML e Lua são membros editáveis; nenhum XML isolado é asset sem
registro no bundle. Esse formato diffável será fechado em R01.

Bundle cooked é um container binário `.rdb`, com header versionado, índice de
registros tipados, offsets/tamanhos, hashes e referências entre assets/bundles.
Não persistir ponteiros/padding/estruturas C++ cruas. Lua permanece fonte Lua em
registros do bundle, sem tradução para código nativo ou bytecode obrigatório.

IR é o modelo intermediário comum. Importadores XML só existem nos targets de
autoria. O runtime instancia descritores compilados usando registradores de
componentes e scripts, com IDs estáveis e versão de schema/binding. C++ gerado é
dado declarativo e registros, sem copiar comportamento Lua para C++ e sem injetar
texto XML como código arbitrário. Strings e identificadores passam por escaping.

C++ gerado e bundle cooked derivam da mesma IR. Descritores compilados referenciam
payloads por ID; não há duas fontes de autoria. Build/catalog ID e hashes impedem
misturar executável e bundles de gerações incompatíveis. Alterar somente Lua não
recompila C++ quando o contrato de binding permanece igual.

O loader valida versões, limites, offsets, checksums e dependências antes de expor
dados. Handles/pinning protegem registros em uso durante unload; scripts/cenas não
guardam ponteiros para memória desmontada. Sem fallback Shipping para XML.

Editores/inspetor em C# acessam serviços de autoria/cooking por protocolo CLI/IPC
versionado e schemas/DTOs; não portam parser e cooker para outra implementação.
Inspeção mostra referências diretas/transitivas e reversas ('quem usa?'), tipos,
tamanhos e origem. Hierarquias cíclicas são inválidas; referências mútuas permitidas
pelo schema são agrupadas no grafo, sem declarar todo ciclo um erro.

Geração usa uma ferramenta host, manifesta todas as dependências, escreve em
`build/.../generated/` e substitui saídas atomicamente somente se o conteúdo mudou.
O fluxo inicial suporta build nativo Windows; cross-compilation fica fora da
entrega. Build sempre inclui o registro central de assets, evitando que o linker
descarte descritores não referenciados. Arquivos gerados não são versionados.

Development pode carregar XML por serviço de autoria para preview; também precisa
executar os assets cooked para comparar os dois caminhos. Shipping nunca depende
de XML, diretórios de fontes ou do editor. Recebe bundles cooked, não Lua solto.
Manifests e membros autorados são versionados; bundles cooked e C++ gerado não são.
Um manifesto informa hashes, versões e
dependências dos assets e scripts. Cooking falha antes de compilar se algo faltar.

### Estrutura de destino, criada aos poucos

```text
engine/{core,runtime,platform/console,render,physics,ui,scripting,assets}/
game/{include,src}/
apps/preview_host/             # C++20 preview process; no editor UI
tools/{assetc,authoring,pipeline}/  # C++20/Python pipeline tools
tools/dotnet/{Raiders.Tools.Contracts,Raiders.Tools.Common}/
tools/dotnet/{Raiders.UIEditor,Raiders.EntityEditor,Raiders.BundleInspector}/
assets/ui.bundle/{manifest.json,layouts/,scripts/}
assets/gameplay.bundle/{manifest.json,entities/,scenes/,sprites/,scripts/}
tests/{core,runtime,game,physics,ui,scripting,assets,pipeline,integration,fixtures}/
tests/dotnet/
schemas/
cmake/
docs/
.github/workflows/
```

Bibliotecas nativas usam `include/` e `src/`; projetos C# usam `.csproj`/namespaces.
Os manifests de bundle contam no orçamento de arquivos de cada PR que adiciona
assets. Não criar essa árvore vazia nem mover todos os arquivos num PR.

## 4. Contrato de execução do plano

Cada ID abaixo representa **um PR**, não um milestone gigante. Meta: 3-8 caminhos
alterados; teto de aproximadamente 10, contando testes, documentação, CMake,
fixtures e arquivos gerados de autoria. Um rename conta como um caminho lógico,
mas sua origem e destino devem aparecer no diff e a revisão deve permanecer pequena.
Caso o Git apresente rename como delete/add, usar essa contagem mais conservadora.
Se a previsão ultrapassar 10, dividir em sufixos `a`, `b`, etc. antes de implementar.

Os caminhos indicados são um orçamento concreto de escopo, não uma autorização
para editar todos os arquivos de uma pasta. Para caminhos agrupados com `{a,b}`,
contar cada expansão. O orçamento inclui pontos de integração já existentes.
Reservar até 2 caminhos adicionais para registro de testes/targets quando não
estiverem enumerados; ainda assim o total deve permanecer em até 10. Não usar
globs de fontes para esconder a necessidade de manter CMake explícito.
Nenhum PR deve ficar quebrado aguardando o próximo. Adaptadores temporários só
existem quando necessários para manter o jogo executável e têm remoção planejada.

Verificar branch e working tree antes de agir. Em `master`, sincronizar por
fast-forward seguro e criar branch; em branch de trabalho, continuar a atividade
até sua conclusão. Ao terminar cada atividade: revisar, stage de todos os arquivos
submetíveis e mensagem de commit sugerida. Não fazer commit automaticamente.
`AGENTS.md` e outros arquivos exclusivos do ambiente ficam em exclusões locais.

As seções são agrupamentos de leitura. Dependências por ID determinam a ordem.
As ferramentas de pipeline têm paths C++ como hipótese inicial; escolher Python
para alguma delas exige atualizar paths, mantendo contratos, testes e orçamento.
Sem duração artificial: estimativas de tempo ficam para depois dos primeiros PRs.

## 5. Backlog de PRs pequenos

### A. Base reproduzível e proteção do legado

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
Arquivos (até 8): `renderer.cpp`, `renderer.h`, `PowerUp.cpp`, `PlayField.cpp`,
`GameObject.h`, `GameMenus.h`, `tests/CMakeLists.txt`, `tests/core/header_tests.cpp`.
Entrega: incluir a própria declaração primeiro e declarar dependências reais.
Aceite: headers públicos compilam isoladamente sem unity build/PCH esconder erros;
não introduzir includes de `.cpp`. Se aparecer ciclo estrutural, dividir por header.

**A07 - Corrigir ownership e filas de remoção.** Depende: A06.
Arquivos (5): `PlayField.h`, `PlayField.cpp`, `renderer.h`, `tests/CMakeLists.txt`,
`tests/runtime/legacy_lifetime_tests.cpp`.
Entrega: ownership único, limpeza de filas/objetos/modo e proibição de cópia de
donos de recursos. Remoção é idempotente; objetos removidos não pontuam duas vezes.
Aceite: destruir mundo com filas pendentes, trocar modo e repetir remoção é seguro.

**A08 - Separar tempo simulado de pacing legado.** Depende: A07.
Arquivos (5): `VSync.h`, `SpaceRaiders.cpp`, `GameMode.h`,
`tests/CMakeLists.txt`, `tests/runtime/legacy_time_tests.cpp`.
Entrega: relógio monotônico, timestep fixo e sleep limitado a valor não negativo.
Aceite: ticks equivalentes independem da demora do renderer; time attack termina
no tick previsto; atraso grande possui limite explícito de recuperação.

**A09 - Corrigir alien/player e transições de fim de jogo.** Depende: A08.
Arquivos (7): `Alien.h`, `PlayerShip.h`, `GameMode.h`, `PlayField.cpp`,
`SpaceRaiders.cpp`, `tests/CMakeLists.txt`, `tests/game/legacy_rules_tests.cpp`.
Entrega: limites, morte ao alien chegar ao fundo, destruição de ambos em contato,
congelamento da cena final e prioridade das condições de término/novas ondas.
Aceite: nenhum modo repovoa uma partida encerrada; posição final permanece visível.

**A10 - Corrigir colisões e orçamento de lasers.** Depende: A09.
Arquivos (6): `PlayerLaser.h`, `PlayerDiagonalLaser.h`, `AlienLaser.h`,
`PlayField.h`, `BlockWall.h`, `tests/game/legacy_rules_tests.cpp`.
Entrega: slots coerentes para tiro triplo, laser contra laser, uma resolução por
contato e deslocamento de parede sem recursão. Aceite: slots retornam ao despawn,
uma colisão não duplica score/efeito, parede sobreposta não trava a atualização.

**A11 - Corrigir evolução e buffs temporários.** Depende: A10.
Arquivos (6): `Alien.h`, `PlayerShip.h`, `PowerUp.h`, `PowerUp.cpp`, `Input.h`,
`tests/game/legacy_rules_tests.cpp`.
Entrega: chance de transformação depois da energia, cadência de alien melhorado,
expiração/renovação de buffs, pesos de drop explícitos e cast verificado.
Aceite: testar fronteiras com RNG controlado; buff expira; mesma sequência de
inputs/seed produz mesmos eventos no mesmo build. Não depender de sequência RNG
idêntica entre bibliotecas padrão de compiladores diferentes.

### B. Extração incremental da engine e bibliotecas

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

### C. Mundo por composição e serviços de simulação

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

### D. Rendering por layers e sprites editáveis

**D01 - Ordenação estável e clipping.** Depende: C02, B04.
Arquivos (6): `engine/render/include/raiders/{RenderCommand,RenderQueue}.h`,
`engine/render/src/RenderQueue.cpp`, `engine/render/CMakeLists.txt`,
`engine/platform/console/ConsoleRenderer.cpp`, `tests/core/render_queue_tests.cpp`.
Aceite: background, gameplay, efeitos e UI sobrepõem corretamente; empate é estável.

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

### E. Física 2D pequena e testável

**E01 - Corpos e integração.** Depende: C03.
Arquivos (6): `engine/physics/{CMakeLists.txt,include/raiders/RigidBody2D.h,
include/raiders/PhysicsWorld.h,src/PhysicsWorld.cpp}`, `CMakeLists.txt`,
`tests/physics/integration_tests.cpp`.
Aceite: força/massa geram aceleração; sem força e sem damping mantém inércia;
corpos estáticos não se movem; timestep é o do loop fixo.

**E02 - Colisores e filtros.** Depende: E01.
Arquivos (5): `engine/physics/include/raiders/{Collider2D,CollisionFilter}.h`,
`engine/physics/src/PhysicsWorld.cpp`, `engine/physics/CMakeLists.txt`,
`tests/physics/collider_tests.cpp`.
Entrega: círculo/AABB, masks e sensores, consultas sem dependência do jogo.
Aceite: pares filtrados não colidem; tocar borda tem convenção testada.

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

### F. Modelo de assets e autoria XML

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
política explícita; runtime não linka parser XML.

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

### G. Módulo nativo Lua e contrato de scripting

**G01 - Integrar a VM Lua como dependência de target.** Depende: B07.
Arquivos (6): `cmake/Dependencies.cmake`, `engine/scripting/{CMakeLists.txt,
include/raiders/LuaRuntime.h,src/LuaRuntime.cpp}`, `CMakeLists.txt`,
`tests/scripting/runtime_tests.cpp`.
Entrega: versão fixada e licença registrada, VM com RAII e inicialização/fechamento
explícitos. Aceite: script mínimo retorna resultado e erro não encerra o processo.

**G02 - Definir módulo e diagnósticos de scripting.** Depende: G01.
Arquivos (5): `engine/scripting/include/raiders/{ScriptApi,ScriptError}.h`,
`engine/scripting/src/LuaRuntime.cpp`, `tests/scripting/error_tests.cpp`,
`docs/scripting.md`.
Entrega: versão da API nativa, callbacks protegidos e traceback contextualizado.
Aceite: erro de sintaxe/runtime é atribuível ao script e callback, sem stack Lua
suja; política inicial desabilita a instância com erro até reinício/reload.

**G03 - Binding de entidades por handles.** Depende: G02, C03.
Arquivos (5): `engine/scripting/{include/raiders/WorldBindings.h,
src/WorldBindings.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/world_bindings_tests.cpp`, `tests/fixtures/world_script.lua`.
Entrega: lookup, Transform e spawn/despawn por comandos. Aceite: handle invalidado
retorna erro controlado; Lua não mantém objeto nativo vivo indevidamente.

**G04 - Componentes de script e ciclo de vida.** Depende: G03.
Arquivos (6): `engine/scripting/include/raiders/{LuaScript,ScriptSystem}.h`,
`engine/scripting/src/ScriptSystem.cpp`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/lifecycle_tests.cpp`, `tests/fixtures/lifecycle.lua`.
Aceite: create/start/fixed-update/destroy têm ordem definida; callbacks não rodam
depois de destruir; reiniciar uma cena não mantém listeners ou referências antigas.

**G05 - Binding de física e eventos.** Depende: G04, E04.
Arquivos (5): `engine/scripting/{include/raiders/PhysicsBindings.h,
src/PhysicsBindings.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/physics_bindings_tests.cpp`, `tests/fixtures/physics_script.lua`.
Entrega: forças, velocidade, consultas e contatos; componentes obrigatórios são
verificados. Aceite: callback pode pedir destruição sem invalidar a física.

**G06 - Serviços de gameplay e limites de execução.** Depende: G05, C04.
Arquivos (6): `engine/scripting/{include/raiders/GameplayBindings.h,
src/GameplayBindings.cpp}`, `engine/scripting/src/LuaRuntime.cpp`,
`engine/scripting/CMakeLists.txt`, `tests/scripting/capability_tests.cpp`,
`docs/scripting.md`.
Entrega: input snapshot, RNG do mundo, timers e eventos; módulos de I/O/processo
fora da API normal. Aceite: loop Lua infinito é interrompido por orçamento;
memória/recursão têm limite e diagnóstico. Isso contém erros de autoria; não se
declara isolamento suficiente para executar código hostil de terceiros.

**G07 - Resolver módulos e separar UI de gameplay.** Depende: G06.
Arquivos (5): `engine/scripting/{include/raiders/ScriptResolver.h,
src/ScriptResolver.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/module_tests.cpp`, `docs/scripting.md`.
Aceite: `require` respeita raízes e case; UI não acessa o World diretamente;
comunicação entre contextos usa comandos/eventos e módulos permitidos.

**G08 - Validação de scripts no assetc.** Depende: G07, F06.
Arquivos (5): `tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`tools/authoring/src/AssetGraph.cpp`, `tests/assets/script_validation_tests.cpp`,
`tests/fixtures/invalid_script.lua`.
Entrega: compilar sintaxe sem executar gameplay e validar referências de módulos
e handlers declarados. Aceite: erro de sintaxe/import/script ausente falha antes
do cooking; handlers dinâmicos têm validação complementar em instanciação.

**G09 - Reload de scripts em desenvolvimento.** Depende: G08.
Arquivos (5): `engine/scripting/include/raiders/ScriptSystem.h`,
`engine/scripting/src/ScriptSystem.cpp`, `engine/scripting/src/ScriptResolver.cpp`,
`tests/scripting/reload_tests.cpp`, `docs/scripting.md`.
Entrega: recompilar antes de substituir, reiniciar instâncias explicitamente e
limpar listeners. Aceite: reload inválido mantém versão anterior; sem tentativa
de preservar qualquer estado arbitrário de VM. Shipping não precisa desse serviço.

### H. Runtime de UI com layout e comportamento

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
runtime recebe descritor, não chama parser XML. Fixture XML entra só no teste via autoria.

### I. Cooking, geração C++ e instanciação compilada

**I01 - Fixar ABI lógica dos descritores e registradores.** Depende: F01, C03, G04.
Arquivos (6): `engine/assets/include/raiders/{AssetDescriptors,ComponentRegistry}.h`,
`engine/assets/src/ComponentRegistry.cpp`, `engine/assets/CMakeLists.txt`,
`tests/assets/registry_tests.cpp`, `docs/assets.md`.
Entrega: representações C++ de entidades/UI/cenas, tipos de componente e versão de
binding, sem dependência do parser. Aceite: tipo/propriedade desconhecida falha com
diagnóstico; ordem de registro não muda IDs persistidos.

**I02 - Gerar C++ de entidades.** Depende: I01, F06, R02.
Arquivos (6): `tools/assetc/{include/CppEmitter.h,src/CppEmitter.cpp}`,
`tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`tests/assets/entity_codegen_tests.cpp`, `tests/fixtures/codegen_entity.xml`.
Aceite: mesmo input produz mesmos bytes; nomes/strings são escapados; conflitos
de símbolo têm erro; saída gerada compila e contém dados esperados.

**I03 - Gerar C++ de UI e referências Lua.** Depende: I02, H07, G08.
Arquivos (4): `tools/assetc/src/CppEmitter.cpp`,
`engine/assets/include/raiders/AssetDescriptors.h`,
`tests/assets/ui_codegen_tests.cpp`, `tests/fixtures/codegen_ui.xml`.
Aceite: descritor gerado cria mesma árvore/handlers que a autoria; script é
referenciado por ID/caminho do pacote, sem incorporar XML ou compilar Lua em C++.

**I04 - Gerar C++ de sprites e cenas.** Depende: I03, F08.
Arquivos (6): `tools/assetc/src/CppEmitter.cpp`,
`engine/assets/include/raiders/AssetDescriptors.h`,
`tests/assets/scene_codegen_tests.cpp`, `tests/assets/sprite_codegen_tests.cpp`,
`tests/fixtures/codegen_scene.xml`, `tests/fixtures/codegen_sprite.xml`.
Entrega: buffers de células, instâncias, overrides e referências estáveis no
catálogo gerado. Aceite: saídas compilam, não contêm caminhos da máquina e
reconstituem a mesma cena/sprite; nenhuma fonte XML é necessária na instanciação.

**I05 - Manifesto e grafo incremental de cooking.** Depende: I04, F05, R04.
Arquivos (6): `tools/assetc/{include/CookManifest.h,src/CookManifest.cpp}`,
`tools/assetc/src/main.cpp`, `tools/assetc/CMakeLists.txt`,
`tests/assets/incremental_cook_tests.cpp`, `docs/assets.md`.
Entrega: hashes, versões do gerador/schema, depfile, remoção de saídas órfãs e
write-if-different. Aceite: mudar dependência invalida consumidor; apagar/renomear
asset não deixa registro velho; tocar arquivo sem alterar conteúdo não recompila tudo.

**I06 - Conectar geração ao grafo de CMake.** Depende: I05.
Arquivos (5): `cmake/CookAssets.cmake`, `CMakeLists.txt`, `game/CMakeLists.txt`,
`tools/assetc/CMakeLists.txt`, `tests/assets/build_graph_tests.cmake`.
Entrega: ferramenta host construída antes de executar, outputs/byproducts/depends
declarados, target `raiders_assets_cooked`, sem caminhos absolutos gerados no fonte.
Aceite: build limpo, paralelo, segundo build sem trabalho e rebuild incremental;
apagar saída a regenera. Mudanças de scripts revalidam/copiam sem rebuild C++ desnecessário.

**I07 - Factory de entidades e cena compilada.** Depende: I06, E03, G04.
Arquivos (6): `engine/assets/{include/raiders/EntityFactory.h,
src/EntityFactory.cpp,include/raiders/SceneDescriptor.h}`,
`engine/assets/CMakeLists.txt`, `tests/assets/entity_factory_tests.cpp`,
`tests/fixtures/factory_entity.xml`.
Entrega: World instancia componentes e scripts pelos registradores; rollback
se alguma etapa falhar. Aceite: entidade incompleta não fica viva; referências
entre entidades de uma cena resolvem após criar IDs de todas elas.

**I08 - Comparar autoria e caminho cooked.** Depende: I07, H07.
Arquivos (5): `tests/integration/cooked_parity_tests.cpp`,
`tests/fixtures/parity_entity.xml`, `tests/fixtures/parity_ui.xml`,
`tests/CMakeLists.txt`, `docs/assets.md`.
Aceite: comparar estado instanciado e sequência de eventos entre IR importada e
descritores compilados; teste não se limita a procurar texto dentro do C++ gerado.

**I09 - Catálogo e compatibilidade do runtime.** Depende: I08.
Arquivos (5): `engine/assets/{include/raiders/AssetCatalog.h,src/AssetCatalog.cpp}`,
`engine/assets/CMakeLists.txt`, `tools/assetc/src/CppEmitter.cpp`,
`tests/assets/catalog_tests.cpp`.
Entrega: registro central referenciado pelo executável e validação de versões.
Aceite: Release com otimização mantém todos os assets necessários; schema/API
incompatível falha cedo; não depende de ordem de inicializadores globais.

### J. Base C# compartilhada das ferramentas

**J01 - Definir UX desktop e fronteiras C#/C++.** Depende: F05, Q01, R01.
Arquivos (3): `docs/editors.md`, `docs/entity-format.md`, `docs/tooling-protocol.md`.
Entrega: escolher framework desktop .NET para Windows; canvas, árvore, inspector,
documentos e protocolo de preview. Console continua backend do jogo.
Aceite: desenhar nave, montar UI e inspecionar dependências são fluxos definidos;
a ferramenta C# tem entry point/event loop próprios, sem herdar Application C++.

**J02 - Shell C# e documentos de bundle.** Depende: J01, Q02, R02.
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

**J07 - Host nativo de preview.** Depende: J06, I09, G09, R06.
Arquivos (6): `apps/preview_host/{CMakeLists.txt,PreviewApplication.cpp,
PreviewProtocol.h,PreviewProtocol.cpp}`, `CMakeLists.txt`,
`tests/integration/preview_host_tests.cpp`.
Entrega: processo C++20 com a Application da engine, sem UI de editor, para montar
bundle, iniciar/pausar/resetar preview. Aceite: falha Lua não encerra frontend;
IPC tem versão/timeouts; reset invalida handles. Sem reimplementar física/Lua em C#.

**J08 - Cliente C# de preview.** Depende: J07.
Arquivos (5): `tools/dotnet/Raiders.Tools.Common/{PreviewClient.cs,
PreviewSession.cs,PreviewSurface.cs}`, `tests/dotnet/Raiders.Tools.Tests/PreviewTests.cs`,
`docs/editors.md`.
Entrega: gerenciar processo/input/frames; preview de console pode ser uma grade
de células do host num canvas C#. Aceite: cancelamento/shutdown não deixam processo
órfão; backpressure impede bloquear a UI; estado executado não modifica autoria.

### K. Editor C# de UI e geração segura de Lua

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

**K05 - Handlers e preview de UI.** Depende: K04, H06, J08.
Arquivos (4): `tools/dotnet/Raiders.UIEditor/{UiPreviewController.cs,
EventBindingInspector.cs,UiEditorDocument.cs}`,
`tests/dotnet/Raiders.Tools.Tests/UiPreviewTests.cs`.
Aceite: clique/tween/script rodam no host nativo; parar descarta estado de execução;
erro tem asset/callback/linha e não perde o documento.

### L. Editor C# de entidades e sprites

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

### M. Migração do jogo para assets e Lua

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

### N. Modo de nave com propulsão e meteoros

**N01 - Especificar controles e regras do novo modo.** Depende: M01, E04.
Arquivos (2): `docs/meteor-mode.md`, `docs/legacy-game.md`.
Entrega: definir orientação inicial, propulsão/reverso, eventual rotação, limites
ou wrap, massa/damping, dano/vidas, score, tamanhos e limite de meteoros.
Premissa: manter inércia e permitir frear com impulso oposto; não adicionar rotação
como requisito silencioso. Aceite: tabela de ações e exemplos jogáveis sem ambiguidade.

**N02 - Nave com propulsão e frenagem em Lua.** Depende: N01, M02, L04.
Arquivos (5): `assets/gameplay.bundle/entities/ship.xml`,
`assets/gameplay.bundle/sprites/ship.xml`, `assets/gameplay.bundle/scripts/ship.lua`,
`tests/game/thrust_tests.cpp`, `docs/meteor-mode.md`.
Aceite: acelera por força, mantém inércia e reduz velocidade com reverso, sem
teletransportar Transform; desenho foi aberto e salvo pelo editor.

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

### O. CI seletivo, qualidade e testes entre módulos

O CI mínimo nasce em A05; estes PRs o ampliam quando os respectivos módulos existem.
Não esperar o fim do produto para detectar erros de integração.

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

**O06 - Desempenho medido e orçamento simples.** Depende: N05, P03.
Arquivos (5): `tests/integration/performance_scenarios.cpp`,
`tests/CMakeLists.txt`, `CMakePresets.json`, `docs/performance.md`,
`.github/workflows/ci.yml`.
Entrega: medir tick, apresentação, memória, startup e tamanho do pacote em cenários
fixos; resultados temporários ficam como artefato CI/local, não no fonte.
Aceite: escolher limites a partir de medidas; execução ampla em release/manual,
sem teste de tempo frágil bloquear todo PR em runner compartilhado.

### P. Packaging e entrega

**P01 - Perfis Development e Shipping.** Depende: I09, G08, R06.
Arquivos (5): `CMakePresets.json`, `cmake/BuildProfiles.cmake`,
`CMakeLists.txt`, `game/CMakeLists.txt`, `docs/distribution.md`.
Entrega: opções explícitas para ferramentas, preview XML, logs e reload; Shipping
liga catálogo cooked e desliga parser/editores. Aceite: dependências do executável
Shipping não incluem biblioteca XML nem binários de autoria.

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

**P06 - Guia de desenvolvimento e entrega final.** Depende: O05, O06, P05, S06, R07, Q03.
Arquivos (até 6): `README.md`, `docs/{development,editors,scripting,
assets,distribution}.md`.
Entrega: caminho completo de checkout a build, autoria, testes, cooking e pacote;
troubleshooting real e limitações da engine. Aceite: desenvolvedor em ambiente
limpo reproduz tudo sem configurações pessoais ou instruções locais do agente.

**P07 - Distribuição das ferramentas C#.** Depende: P04, J08, S06, Q03.
Arquivos (6): `tools/pipeline/package_tools.py`,
`tests/pipeline/test_tools_package.py`, `tools/dotnet/Directory.Build.targets`,
`tests/integration/tools_package_smoke.py`, `docs/distribution.md`,
`.github/workflows/release.yml`.
Entrega: dotnet publish dos projetos selecionados, host nativo e CLI de pipeline
compatíveis, versões/contratos e licenças; decidir framework-dependent ou self-contained.
Aceite: pacote de ferramentas separado do jogo abre bundle e preview em diretório
limpo; não depende de SDK/compiler no usuário final. P05 habilita publicação depois
de validar esse pacote; este PR só prepara artefatos/testes.

### Q. Build multilinguagem e isolamento de projetos

**Q01 - ADR de build: CMake/.NET versus Bazel.** Depende: A01.
Arquivos (2): `docs/build-system.md`, `docs/decisions/0001-monorepo-build.md`.
Entrega: comparar C++20, C#/.NET/NuGet, Python, geração, Windows, IDE/debug,
targets isolados, cache e incrementalidade. Spike pequeno em diretório temporário
com biblioteca nativa, projeto C# e input gerado; registrar resultados no ADR.
Aceite: decisão fundamentada, sem dupla manutenção do grafo. A hipótese dos paths
deste plano é CMake/.NET; escolher Bazel exige ajustar esses PRs antes de implementar,
não manter duas descrições completas. Nenhuma migração é feita por sugestão apenas.

**Q02 - Versões e políticas do SDK .NET/Python.** Depende: Q01.
Arquivos (6): `global.json`, `Directory.Build.props`, `Directory.Packages.props`,
`NuGet.config`, `pyproject.toml`, `.gitignore`.
Entrega: fixar SDKs suportados, política de dependências/locks e análise C#; outputs
bin/obj e ambientes Python ignorados. Aceite: selecionar versões verificadas na
implementação, sem forçar .NET para build somente C++; C# não é gerado por CMake.

**Q03 - Solution e filtros por ferramenta.** Depende: J02, K01, L01, S01.
Arquivos (5): `Raiders.Tools.sln`, `tools/dotnet/{UIEditor,EntityEditor,
BundleInspector}.slnf`, `docs/build-system.md`.
Entrega: workspaces de cada aplicação e dependências reais, sem abrir todo o
monorepo. Aceite: IDE/debug e build de cada .csproj/filtro funcionam separadamente.

**Q04 - CLI Python do monorepo por projeto.** Depende: Q01, Q02, J02.
Arquivos (5): `tools/pipeline/build.py`, `tools/pipeline/projects.json`,
`tests/pipeline/test_build.py`, `pyproject.toml`, `docs/build-system.md`.
Entrega: build/test/cook/package por projeto, delegando a CMake ou dotnet; seleção
de targets e argumentos, sem reconstruir o grafo que já pertence a esses sistemas.
Aceite: engine não constrói C#; editor não constrói outros editores; falha nativa
ou .NET retorna código não zero. É ferramenta de desenvolvedor, não helper de agente.

### R. Data bundles de autoria e runtime

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

**R04 - Escritor de bundle cooked.** Depende: R02, I04, G08.
Arquivos (6): `tools/assetc/{include/BundleWriter.h,src/BundleWriter.cpp}`,
`tools/assetc/CMakeLists.txt`, `tools/assetc/src/main.cpp`,
`tests/assets/bundle_writer_tests.cpp`, `docs/data-bundles.md`.
Entrega: header, índice, payloads, Lua fonte, grafo e hashes; descritores C++ e
bundle compartilham catalog ID. Aceite: saída estável sem paths da máquina,
offsets/alinhamento definidos e falha não substitui bundle válido.

**R05 - Leitor binário C++20.** Depende: R04, I01.
Arquivos (6): `engine/assets/{include/raiders/BundleReader.h,src/BundleReader.cpp,
include/raiders/BundleFormat.h}`, `engine/assets/CMakeLists.txt`,
`tests/assets/bundle_reader_tests.cpp`, `tests/fixtures/corrupt.rdb`.
Aceite: rejeitar truncamento, overflow/offset inválido, versão desconhecida,
hash divergente e ID duplicado antes de acesso; não executar código ao inspecionar.

**R06 - Catálogo montado e ciclo de vida.** Depende: R05, I09.
Arquivos (6): `engine/assets/{include/raiders/BundleStore.h,src/BundleStore.cpp}`,
`engine/assets/src/AssetCatalog.cpp`, `engine/assets/CMakeLists.txt`,
`engine/scripting/src/ScriptResolver.cpp`, `tests/assets/bundle_lifetime_tests.cpp`.
Entrega: montar dependências, lookup por ID e carregamento de Lua de registros.
Aceite: unload com assets em uso é adiado/recusado explicitamente; mismatch entre
binário/bundle falha; cena e scripts não mantêm ponteiros para dados desmontados.

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

### S. Inspetor C# de bundles e dependências

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

## 6. Regras de seleção de CI

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

## 7. Pontos de integração e sequência recomendada

1. **Base confiável:** A01, Q01 e A02-A11; CI mínimo começa em A05. A02/A03 são PRs próprios,
   não um grande arrasto de exclusões junto da refatoração.
2. **Engine utilizável:** B01-B11, C01-C05 e D01-D03. O01/O02 entram assim que
   as bibliotecas existirem. O jogo deve continuar iniciando a cada PR.
3. **Fundação de conteúdo:** E01-E04, F01-F08, G01-G09 e R01-R03;
   decidir SDKs/pipeline em Q02 e respeitar as dependências.
   Não é necessário terminar toda a física para começar a VM Lua ou o schema UI.
4. **Primeira entrega vertical de assets:** H01-H07, I01-I09 e R04-R08. Uma UI
   e uma entidade atravessam bundle/XML -> IR -> C++ + bundle cooked -> runtime,
   com Lua real e testes do loader binário.
5. **Autoria utilizável:** J01-J08, K01-K05, L01-L05, S01-S06 e Q03/Q04; antecipar M01 para registrar
   extensões do jogo no editor. Fazer o fluxo de uma tela
   e uma entidade de ponta a ponta antes de adicionar novas paletas de componentes.
6. **Jogo por dados:** M02-M10. Migração em fatias, com remoções em PRs próprios.
7. **Novo gameplay:** N01-N05; ferramentas já devem conseguir produzir seus assets.
8. **Distribuição:** iniciar P01/P02 quando os descritores estiverem estáveis;
   completar P03-P07 e O03-O06 com os modos, editores C# e inspetor funcionando.

Esses pontos são demonstrações do produto, não PRs agrupando os arquivos da fase.
Se uma mudança descobrir problema anterior, abrir uma correção pequena e atualizar
a dependência; não aumentar silenciosamente o escopo do PR corrente.

## 8. Estratégia de testes e critérios finais

- Testes unitários: ownership, handles, tempo, RNG, contatos, layout, callbacks e
  preservação de código customizado. Usar relógio/input/RNG controlados onde importa.
- Testes de integração: aplicação mínima sem jogo, instanciação de entidade/UI,
  equivalência autoria/cooked, referências Lua e ciclo de cena/partida.
- Testes de ferramentas: XML inválido, round-trip, undo/redo, conflitos de save,
  geração interrompida e build incremental com dependências reais.
- Regressão de gameplay: modos de aliens, seed/input, tiros, buffs, score e
  game over; modo de meteoros com propulsão, frenagem e fragmentação finita.
- Smoke visual manual: jogo, editores C# e inspetor de grafo; foco, arrastar, clipping, cores,
  reabrir documento e preview. Guardar capturas temporárias localmente ou em CI.
- Bundles: corrupção/truncamento, versões, offsets, hashes, referências reversas,
  montagem/unload, transações de save e compatibilidade executável/catalog ID.
- Monorepo: abrir/build/test por projeto nativo ou C#, sem construir frontends
  independentes; contratos CLI/IPC são testados entre processos reais.
- Distribuição: checkout limpo, build Shipping e execução em diretório sem fontes,
  sem XML e sem dependências acidentais do ambiente de desenvolvimento.

O resultado estará completo quando a engine puder rodar uma aplicação sem o jogo;
o jogo for uma biblioteca ligada ao executável com entry point da engine; entidades
e UI forem autoradas nos editores e instanciadas por descritores cooked; gameplay
e UI tiverem comportamento Lua; salvar UI preservar código customizado; e o pacote
rodar os dois modos com CI seletivo, testes e instruções reproduzíveis. Todos os
dados autorados e distribuídos pertencem a bundles; ferramentas interativas são
C# e permitem navegar visualmente entre um asset, suas entidades e consumidores.
Projetos nativos e gerenciados podem ser editados/compilados separadamente.

## 9. Decisões a fechar durante a implementação

| Decisão | Momento | Premissa até a decisão |
| --- | --- | --- |
| Toolchain principal e versões mínimas de CMake | A04 | Windows e C++20; sem assumir caminhos do cache antigo |
| Cadência, duração/stack de buffs e pesos de drops | A11 | Seguir PDF onde explícito e documentar lacunas |
| IDs, schema e formato de sprites/cenas | F01 | XML versionado com ID estável e IR independente de parser |
| Biblioteca XML e forma de integrar Lua | F02/G01 | Dependências fixadas por targets, licença e build reproduzível |
| Framework desktop C# e SDK .NET | J01/Q02 | Aplicações C# Windows; console apenas no jogo/host |
| Build do monorepo | Q01 | Comparar CMake + .NET/MSBuild com Bazel antes de consolidar orquestração |
| Versão/uso de Python no pipeline | Q02/Q04 | Versão suportada fixada; pipeline pode ser C++ ou Python |
| Layout de autoria e formato binário de bundles | R01 | Diretório diffável + manifesto na autoria; .rdb versionado no runtime |
| Como o editor conhece componentes do jogo | J06/L03/M01 | Schema/DTO via pipeline; preview em processo nativo, sem ABI C++ no frontend C# |
| Orientação, eventual rotação e bordas da nave | N01 | Inércia com propulsão/reverso; definir antes de programar controles |
| Limites de desempenho e tamanho | O06 | Medir baseline antes de impor números |
| Lua e distribuição de ferramentas | P02/P07 | Lua fonte como registros de bundle cooked; ferramentas .NET em pacote separado |

Nenhuma dessas decisões pendentes impede o plano ou autoriza uma reescrita global.
Cada uma deve resultar em um contrato pequeno e verificável no PR indicado.
