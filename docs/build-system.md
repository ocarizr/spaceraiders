# Build and environment contract

This is the target build contract selected in
[ADR 0001](decisions/0001-monorepo-build.md), not a list of commands already available
in this checkout. Q01 evaluated the architecture and a disposable spike; production
presets, packages, environment recipes and setup tools are future tasks.
For today's prototype build, use the [A01 reference](legacy-game.md).

## Build ownership

| Area | Owner | Unit a developer selects |
| --- | --- | --- |
| C++20 engine, runtime, preview and native tools | CMake targets; Ninja backend | Library/executable target and its dependencies |
| C# tools and reusable libraries | .NET 10 SDK-style projects, MSBuild and NuGet | `.csproj` or solution filter |
| Python libraries/pipeline | `pyproject.toml`, wheel builder and isolated environments | Package/entry point and its locked requirements |
| Lua gameplay/UI | LuaJIT and scripting/content toolchain | Source module/project; packaging profile for bytecode |
| Cross-project build/test/package | Thin project-oriented coordinator | Requested product and explicit artifact prerequisites |
| Workstation setup | C#/.NET 10 environment manager and shared headless core | Selected environment profiles |

CMake does not own C# project generation. SDK-style `.csproj` files remain the
managed source of truth for IDE and build. CMake presets hold shared native
configurations; local discovery/overrides stay in ignored user configuration.
[CMake preset ownership](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).

Workflows and the graphical setup tool use the same profile/recipe contracts as
the command line. Do not build unrelated editors simply because they share a
repository. If a requested target needs a generator, schema package or native
preview library, include that dependency explicitly rather than promising an
artificially dependency-free build.

The following are **future examples**, not implemented Q01 commands:

```powershell
cmake --build --preset windows-debug --target raiders_runtime
dotnet build tools/dotnet/Raiders.UIEditor/Raiders.UIEditor.csproj --no-restore
.venv/pipeline/Scripts/python.exe -m raiders_pipeline build --project ui-editor
```

SDK patch/tool versions and valid preset names are fixed in Q02/A04. The managed
baseline is .NET 10: reusable platform-neutral libraries may target `net10.0`,
while Windows desktop projects use the appropriate Windows target, for example
`net10.0-windows`. The Q01 WPF build was an SDK/XAML capability check, not the final
desktop framework choice. See Microsoft's
[desktop SDK properties](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props-desktop).

## Managed library and NuGet boundaries

Create real library subprojects when a reusable dependency is needed. They own
their API/tests and can produce one `.nupkg` per package boundary. Shared artifacts
are consumed using `PackageReference`, with central versions and application lock
files; neither loose copied DLLs nor a CMake-generated C# dependency graph is the
default. Closely coupled private modules within one product may use project
references when they are not intended to be separate packages.

The package producer builds/packs first. The consumer then restores the intended
version from a local build feed or approved internal feed and builds independently.
Changing library source does not magically update a consumer of an old package:
the coordinator must identify affected consumers and use an explicit new package
version. Immutable local prerelease versions or a content/revision stamp prevent
stale package-cache reuse. Never replace different bytes under the same package
ID/version. Commit reviewed version/lock changes, not generated `.nupkg` files.

Use `Directory.Packages.props` for shared package versions and reviewed
`packages.lock.json` where appropriate; CI requires existing lockfiles and locked
restore rather than silently resolving a new graph. Local feeds, generated package
outputs and credentials are separate from source-controlled NuGet configuration.
No Q01 action published to NuGet.org or another external feed.
[Package references](https://learn.microsoft.com/en-us/nuget/consume-packages/package-references-in-project-files),
[central versions](https://learn.microsoft.com/en-us/nuget/consume-packages/central-package-management),
[package creation](https://learn.microsoft.com/en-us/nuget/create-packages/creating-a-package-dotnet-cli).

## Python modules and environments

Package reusable Python code with `pyproject.toml` and a proper import namespace.
A build environment installs pinned packaging/backend requirements, builds the
wheel, then an execution environment installs that wheel and its locked runtime
dependencies. Imported code comes from the installed package, not ad hoc `sys.path`
insertion. An explicitly chosen editable-install mode may serve package development;
CI/release verifies the actual wheel so missing package files are detected.

Pure-Python modules do not need a C++-style compilation step. The package builder
creates an installable distribution; native extensions, if introduced, have an
additional Windows compiler/ABI requirement. Python source/VM bytecode behavior is
also separate from LuaJIT bytecode cooking.
[Python package builds](https://packaging.python.org/en/latest/tutorials/packaging-projects/).

Build and runtime environments can be separate profiles under ignored local paths,
for example `.venv/build` and `.venv/pipeline`. Each recipe records interpreter
identity, architecture, package versions/hashes and requirement fingerprint. An
environment is Ready only if its interpreter and installed packages satisfy that
recipe; existence of a directory is insufficient. A shared runtime environment
may serve compatible pipeline modules; do not create a venv per import needlessly.

Do not commit or copy an existing venv as the setup strategy. Recreate it from a
compatible base interpreter and locked artifacts. The base can be a discovered
installation or a pinned managed download under an ignored `.tools` directory.
Do not require a global Python installation for distributed designer tools: bundle
the needed runtime or ship a packaged/frozen executable. The final interpreter
version/distribution and update policy belong to Q02/Q07, not the spike's installed
Python 3.12.14. [Virtual environment lifecycle](https://docs.python.org/3/library/venv.html).

## Native dependencies, LuaJIT and generation

Native libraries use explicit CMake targets and pinned dependency inputs. LuaJIT
is a native runtime/compiler dependency with a versioned scripting ABI; NuGet is
not a substitute for its native build. SDK/library distributions include required
native binaries and compatibility metadata when crossing into C# tools.

Generated native files declare their outputs/byproducts, schemas, source inputs,
generator executable/package/environment fingerprint and dependencies. The Q01
spike verified an input-file change, not every generator-version invalidation case.
Production checks must cover missing outputs and generator/schema changes as well.
Generated files are shared through one producer, not concurrent independent writers.
[CMake generated-output contract](https://cmake.org/cmake/help/latest/command/add_custom_command.html).

Editing/Play directly deserializes XML/Lua through the source provider. It does not
invoke cooking, C++ generation or bytecode packaging. Explicit content packaging
produces optimized bundles/bytecode; full-game packaging also compiles eligible
generated C++ data. This boundary is unchanged by the build-system decision.

## Environment setup after cloning

The planned C# graphical manager presents profiles and health:
**Ready**, **Missing**, **Needs repair**, **Unsupported** and **Checking/Unknown**.
Select only needed profiles: native development, .NET 10 tooling, Python pipeline,
Lua development, or designer tools. Display detected versions/paths, missing
requirements and a concrete action/result for each selected profile.

Already healthy profiles require no reinstall. Repair addresses the failed recipe
step, preserves source/user configuration, rechecks health and reports partial
failures. The detection/recipe engine is also callable headlessly by CI. Downloads,
version/checksum manifests and diagnostic logs are explicit; machine-local state,
caches and generated environments are ignored.

The manager is self-contained Windows .NET 10 tooling so it can start before the
.NET SDK or Python are installed. A small bootstrap launcher may fetch a versioned,
verified manager artifact; it does not need to be a Python script. SDK/toolchain
installation belongs to a selected developer profile, not a hidden prerequisite
for designers. The [setup project plan](plans/environment-setup.md) specifies the
implementation breakdown. Q01 defines it but does not implement or install it.

## CI, caches and debugging

The coordinator handles artifact-level ordering, selected-project impact and exit
codes. CMake/MSBuild/Python packaging retain ownership of compiler/package details.
Cache keys include toolchain/SDK, configuration, architecture, dependency locks and
generator versions; restore caches and compiled action caches are different things.
This approach does not promise Bazel-equivalent global hermeticity or remote caching.

Test only affected projects plus real consumers of changed public contracts; always
start the required CI gate. Offer tools-only and game/full-packaging products.
Do not add Linux jobs. Keep the native/source editing, packaged content, Lua debugger
and telemetry ON/OFF checks defined by the individual plans.

C++ and C# projects retain normal Windows debug outputs/IDE workflows; Lua uses
VS Code and the embedded-VM debugger. Q01 compiled representative projects but did
not validate an IDE debugger session. Avoid claiming that source debugging or
publishing works solely because a library compiled.
