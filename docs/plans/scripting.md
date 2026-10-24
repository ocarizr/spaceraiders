# LuaJIT runtime and scripting development environment

Status: Planned. Implementation has not started.

## Scope and independent execution

C++20 embedded LuaJIT, gameplay/UI bindings, safe lifecycle, generated API definitions,
VS Code language tooling, debugging and bytecode cooking contracts.
Inputs: World/components, native UI/physics, observability and bundle loader.
Exports: versioned ScriptAPI, source-map/debug protocol, LuaLS definitions and
LuaJIT bytecode compatibility metadata. Gameplay and UI keep separate namespaces.
First slice: load source in the embedded VM, call one registered component and hit
a breakpoint in the real native preview process. A standalone Lua debugger demo is
insufficient. Shipping bytecode and JIT execution are distinct steps.

## Planning and integration contract

See the [macro roadmap](../plano-de-desenvolvimento.md) for shared decisions,
project handoffs and the global task index. This file owns its task definitions.
Implement only ready tasks; external dependencies are explicit task IDs, not a
requirement to finish every other project. Target 3-8 changed paths per PR, at
most about 10 including tests/build registration/manifests. Split before exceeding it.
Detailed notes retained from the prior plan use its approved Portuguese planning
exception; implementation files, diagnostics, API names and shipped docs use English.

## Implementation backlog

**G01 - Embed a pinned LuaJIT runtime.** Depends: B07.
Paths (6): `cmake/Dependencies.cmake`, `engine/scripting/{CMakeLists.txt,
include/raiders/LuaRuntime.h,src/LuaRuntime.cpp}`, `CMakeLists.txt`,
`tests/scripting/runtime_tests.cpp`.
Acceptance: actual embedded LuaJIT runs source and compatible bytecode; RAII VM
lifecycle and error propagation; pin source revision, license, architecture/GC mode
and Lua 5.1-compatible API assumptions. Do not silently depend on Lua 5.4/5.5 APIs.

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


## LuaJIT and authoring contract

Use the same pinned LuaJIT family in game, preview host and distributed content
compiler. Source compilation to VM bytecode happens at cooking; native JIT
compilation happens during execution. Neither is equivalent to C++ AOT compilation.
Stripped bytecode hides plain source/debug names but is not encryption or reliable
source protection. Do not promise universal runtime speedups; measure startup,
warm-up, steady-state execution and JIT cache separately.
[LuaJIT runtime](https://luajit.org/luajit.html),
[bytecode options](https://luajit.org/running.html).

Live editing runs Lua source from the source bundle, with stable chunk names and
source maps for generated headers/custom regions. VM source compilation/JIT during
loading/execution is not an offline bytecode-cooking prerequisite. A separate
packaging-debug profile can retain line data in bytecode for equivalence tests.
Shipping uses validated
stripped bytecode records inside bundles; source and debugger agents are excluded.
Compatibility metadata includes VM bytecode version, build identity and GC/frame
mode; reject mismatches before loading. Deterministic dumps need explicitly
selected supported options, not a generic claim that every bytecode dump is stable.
[Compatibility and dump modes](https://luajit.org/extensions.html).

VS Code is the external code editor, not a new homegrown Lua IDE. Provide a project
workspace, generated API definitions, LuaLS configuration matching LuaJIT,
recommended extensions, formatter/lint tasks and launch/attach configurations.
LuaLS annotations support completion and type diagnostics.
[LuaLS](https://github.com/LuaLS/lua-language-server),
[annotations](https://luals.github.io/wiki/annotations/).

A debugger candidate is actboy168/lua-debug, which lists LuaJIT and breakpoint/
step/watch support. This does not prove attachment to our embedded VM works:
the actual native host, coroutine behavior, native ABI and debugger hook lifecycle
must pass a spike. A different compatible DAP adapter may be chosen if needed.
Debug sessions may disable JIT in the selected VM/functions to obtain reliable
stepping; performance sessions use JIT ON without debugger hooks.
[Debugger capabilities](https://github.com/actboy168/lua-debug).

FFI, debug-library access and native module loading are controlled capabilities.
Designer scripts cannot bypass registered components through unrestricted FFI.
Debugger capabilities belong to development builds only. This is trusted project
scripting; bytecode is not a sandbox for hostile scripts.

**G10 - Bytecode/compiler ABI contract.** Depends: G01, R01.
Paths (4): `schemas/script-bytecode.schema.json`, `tools/assetc/src/ScriptCompiler.cpp`,
`tests/scripting/bytecode_compatibility_tests.cpp`, `docs/scripting.md`.
Acceptance: source and compiler revision/options feed hashes; wrong bytecode
version/GC mode fails clearly; compiler binary matches the runtime shipped to designers.

**G11 - Generate LuaLS definitions from registered bindings.** Depends: G03, G05, H05, G18.
Paths (6): `tools/authoring/{include/LuaApiEmitter.h,src/LuaApiEmitter.cpp}`,
`tools/authoring/CMakeLists.txt`, `tools/assetc/src/main.cpp`,
`tests/scripting/api_definition_tests.cpp`, `docs/lua-development.md`.
Acceptance: components/widgets/events/enums/nullable handles have LuaCATS stubs;
IDE definitions match runtime API version and are not executed as gameplay modules.

**G12 - VS Code workspace, lint and formatting.** Depends: G11, Q02.
Paths (6): `.luarc.json`, `.vscode/{extensions.json,tasks.json}`,
`tools/pipeline/lint_lua.py`, `tests/pipeline/test_lua_lint.py`,
`docs/lua-development.md`.
Acceptance: completion, hover, go-to-definition and diagnostics work for UI/gameplay
and generated widget references; no blanket suppression of unknown globals;
headless CI reports file/line consistently. Pin compatible tool versions when adopted.

**G13 - Debugger spike against the embedded VM.** Depends: G01, J07.
Paths (5): `docs/decisions/0002-lua-debugger.md`,
`engine/scripting/{include/raiders/ScriptDebugSession.h,src/ScriptDebugSession.cpp}`,
`engine/scripting/CMakeLists.txt`, `tests/scripting/debug_session_tests.cpp`.
Acceptance: break in Lua called by native preview; step in/out/over, locals,
watches, conditional breakpoint, coroutine and error handling. Confirm actual
LuaJIT/architecture/adapter compatibility before committing to the debugger.

**G14 - Debug source maps, attach and pause semantics.** Depends: G13, G11, K03.
Paths (6): `.vscode/launch.json`, `engine/scripting/src/ScriptDebugSession.cpp`,
`tools/authoring/src/UiLuaGenerator.cpp`, `schemas/script-source-map.schema.json`,
`tests/scripting/debug_source_map_tests.cpp`, `docs/lua-development.md`.
Acceptance: breakpoints map to authoring Lua including custom regions; reload
rebinds changed scripts; reset clears stale handles; while paused the host pumps
debug messages without advancing physics, timers or watchdog time budgets.

**G15 - Open-code/debug integration from C# tools.** Depends: G12, G14, J06.
Paths (5): `tools/dotnet/Raiders.Tools.Common/{CodeEditorService.cs,
LuaWorkspaceService.cs,DebugSessionController.cs}`,
`tests/dotnet/Raiders.Tools.Tests/LuaWorkspaceTests.cs`, `docs/lua-development.md`.
Acceptance: handler/error opens VS Code at file/line, generates project-local
workspace/API stubs and attaches to the correct preview; missing VS Code/extension
gets an actionable setup message. Never install extensions without user action.

**G16 - Shipping bytecode and development symbols.** Depends: G10, G14, R04.
Paths (6): `tools/assetc/src/ScriptCompiler.cpp`, `tools/assetc/src/BundleWriter.cpp`,
`engine/scripting/src/ScriptResolver.cpp`,
`tests/scripting/bytecode_packaging_tests.cpp`, `tests/fixtures/bytecode.lua`,
`docs/distribution.md`.
Acceptance: source -> deterministic compatible bytecode -> existing runtime with
JIT; Shipping excludes sources/debugger, development preserves maps/line metadata;
measure load time and behavior, including corrupted/incompatible input.

**G17 - Profile JIT execution separately from debugging.** Depends: T08, G16.
Paths (5): `engine/scripting/{include/raiders/LuaSamplingProfiler.h,
src/LuaSamplingProfiler.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/jit_profile_tests.cpp`, `docs/observability.md`.
Acceptance: callback scopes plus LuaJIT CPU samples expose interpreted/JIT/native
time and warm-up/cache effects; profiling and debug modes are clearly labeled.


**G18 - Animation and configurable-render binding API.** Depends: G04, D08, D09.
Paths (5): `engine/scripting/{include/raiders/RenderBindings.h,
src/RenderBindings.cpp}`, `engine/scripting/CMakeLists.txt`,
`tests/scripting/render_animation_bindings_tests.cpp`, `docs/scripting.md`.
Acceptance: scripts play/stop/sample authored clips, receive animation events and
select valid render layers through handles; callbacks cannot retain destroyed
animators or bypass registry validation. Bindings feed generated LuaLS definitions.
