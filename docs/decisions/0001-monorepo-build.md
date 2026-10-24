# ADR 0001: Windows monorepo build ownership

Date: 2026-10-24. Task: Q01. Baseline: `bf4bd7c`.
Status: Proposed decision, ready for maintainer review.

## Decision

Use **CMake with Ninja for native C++20**, **SDK-style .NET 10 projects with
MSBuild/NuGet for C#**, and **packaged Python modules with isolated environments**.
Add a small project-oriented orchestration entry point rather than adopting Bazel
as the repository-wide build system. Native and managed projects retain their
normal IDE/project files. Do not generate C# projects from CMake or maintain an
equivalent second dependency graph in Bazel.

This is a decision about ownership and integration, not a claim that CMake is a
complete replacement for Bazel's multilanguage action graph. The common entry
point delegates compilation/package resolution to each ecosystem and coordinates
only cross-project artifact handoffs. No production build migration is implemented
by this ADR. See the [build contract](../build-system.md).

## Context and constraints

- Windows only; C++20 engine, native preview/runtime and optional native pipeline tools.
- C# developer applications use **.NET 10**, including reusable library projects
  that can be packaged as NuGet dependencies. Desktop framework choice remains J01.
- Python pipeline libraries are real packages: build a wheel, install it in the
  selected environment, then import/run it. A virtual environment is not itself
  a package format or a native compiler.
- LuaJIT is a native dependency and a separate script authoring/bytecode workflow.
- Developers must edit/build one project and its dependencies, not every editor.
- Live editing deserializes XML/Lua directly. Packaging explicitly cooks binary
  data and, for full-game packaging, generates/compiles eligible C++ content.
- Designers receive binaries. A graphical C# environment manager will prepare
  selected developer environments after cloning, independently of game packaging.
- Small changes, Windows debugging, understandable dependencies and reproducibility
  matter more now than an unmeasured future fleet-wide cache benefit.

## Options compared

| Criterion | CMake + .NET/MSBuild + Python packaging | Bazel with language rules |
| --- | --- | --- |
| Native C++20 on Windows | Passed local compile/run and input regeneration using MSVC/Ninja | Passed local compile/run and explicit generation using MSVC/rules_cc |
| Build a selected project | Native target or `.csproj`; unrelated sentinel projects stayed unbuilt | Target closure; unrelated native sentinel stayed unbuilt |
| No-change/incremental native build | Ninja did no work; changing input regenerated/relinked the affected library/application | Action cache reused work; changing input reran generation/dependent actions |
| .NET 10 | Installed SDK path tested with pack, PackageReference restore and consumer execution | Supported by current rules_dotnet source/upstream CI; not rejected as incompatible |
| Windows desktop/XAML | `net10.0-windows` WPF/XAML project compiled with the SDK | Desktop/XAML integration needs additional validation; not demonstrated by a plain C# rule |
| Managed IDE/project ownership | `.csproj` is the same model used by SDK build and IDE | Current rules_dotnet documentation says IDE project files must be maintained separately |
| NuGet libraries | Local `.nupkg` producer/consumer path tested; centralized versions/locks fit the SDK | NuGet support exists through rules/tooling; adoption also changes package-resolution/build definitions |
| Python modules/environments | Wheel built in a build venv and imported from a separate installed-package venv | Python rules/toolchains can participate in a unified graph; that integration was not locally exercised |
| Generated files | Explicit `OUTPUT`, dependencies/byproducts and generator package fingerprints must be maintained | Explicit actions/tools/inputs/outputs; native action tested without Bash |
| Shared action cache | No unified cross-language action cache is provided by this combination; caches require ecosystem-specific policies | Strong reason to use Bazel when a unified action graph/cache is needed |
| Adoption/maintenance here | Keeps native/desktop/package conventions; requires a small artifact coordinator | Adds BUILD/Starlark/toolchain ownership and managed IDE/desktop integration work |

Bazel's target graph and cache are real advantages, including on Windows. Its
Windows support is not a Linux dependency. Some shell-based rules need Bash, but
the native generation spike used `ctx.actions.run` and needed no Bash. See
[Bazel's model](https://bazel.build/about/intro) and
[Windows rule/toolchain guidance](https://bazel.build/configure/windows).

### Managed tooling is the deciding factor

The inspected community `rules_dotnet` revision was
`edcf477f9236406f739d73d4cf06366f9fe8f603` (2026-10-19). Its module registers
.NET 10.0.401 and its CI includes Windows/net10.0. This is evidence of .NET 10
support, not evidence that this project's entire desktop/debug/publish workflow
already works under Bazel.

Its documentation explains that it uses .NET compilers/tools directly rather than
MSBuild, and that IDE support needs separately maintained MSBuild project files.
For a Windows tool suite that must be editable as individual projects, this adds
an immediate second-model maintenance cost. Inspect the pinned
[design and IDE documentation](https://github.com/bazel-contrib/rules_dotnet/blob/edcf477f9236406f739d73d4cf06366f9fe8f603/docs/README.md),
[module/toolchain configuration](https://github.com/bazel-contrib/rules_dotnet/blob/edcf477f9236406f739d73d4cf06366f9fe8f603/MODULE.bazel)
and [CI matrix](https://github.com/bazel-contrib/rules_dotnet/blob/edcf477f9236406f739d73d4cf06366f9fe8f603/.bazelci/presubmit.yml).

The same revision's first-class project SDK list contains default/web/razor/
blazorwasm, not WindowsDesktop. That identifies an integration risk; it does not
prove WPF, WinForms or another desktop framework is impossible with custom work.
The framework decision remains separate. Compare the pinned
[SDK selection source](https://github.com/bazel-contrib/rules_dotnet/blob/edcf477f9236406f739d73d4cf06366f9fe8f603/dotnet/private/sdk/packs.bzl)
with the SDK's documented
[desktop/XAML build path](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props-desktop).

An alternative Bazel wrapper that simply invokes `dotnet build` would retain
MSBuild but still need accurate declared inputs, outputs, restore/toolchain
isolation and cache invalidation. It would add an orchestration layer without
automatically delivering a correct hermetic managed action graph. That is not a
useful default for this repository today.

## Disposable Windows spike

The spike stayed outside the repository. Only this interpretation and the build
contract are deliverables; experimental sources, binaries, caches and logs remain
local. No external package was published.

| Component | Version/input used |
| --- | --- |
| Host/native compiler | Windows x64; MSVC 19.44.35229.0, VS 2022 developer environment |
| Native generator/build tool | CMake 3.31.6-msvc6; Ninja 1.12.1 |
| Managed SDK | .NET SDK 10.0.402; installed reference packs 10.0.13 |
| Python | Bundled Python 3.12.14; isolated build and execution venvs |
| Python build dependencies | setuptools 84.2.1, wheel 0.48.0, packaging 26.3 in the build environment |
| Bazel native test | Official Bazel 9.0.0 Windows binary; rules_cc 0.2.14 |
| Bazel executable integrity | SHA256 `463faee497df2913854d80776784137cb47f42960b4ef4e4f85068c8da4849a8` checked against its release checksum |

Versions record the experiment; Q02 owns production pins, not a blanket upgrade
to whichever versions happened to be installed. .NET **10** is already a confirmed
project requirement. The Python version is not selected by this experiment.

### Observed results

1. A small Python code-generation module was packaged as a wheel in a build venv.
   The wheel was installed without its source tree on the execution venv's import
   path; import resolved to that venv's `site-packages`.
2. CMake built a static library from generated C++ and a C++20 executable using
   `std::span`. Generation invoked the installed Python module. It printed `21`.
3. A no-change CMake rebuild reported `ninja: no work to do`. Changing input `21`
   to `22` reran generation, rebuilt the generated object/library and relinked the
   executable. The `main.cpp` object's timestamp stayed unchanged; output was `22`.
4. A .NET 10 library produced `Q01.Contracts.0.1.0.nupkg` in a local feed. A console
   consumer restored it through `PackageReference`, built and printed `21`.
   Repeated locked restore succeeded; a no-change build preserved the consumer DLL.
5. A separate `net10.0-windows` project with `UseWPF` and `App.xaml` compiled.
   It was not launched or interactively debugged. This is not adoption of WPF for J01.
6. An unrelated C# project containing `#error` was not built when selecting the
   consumer. CMake also left an unrelated native sentinel target unbuilt.
7. Bazel compiled/ran the C++20 target, then a version with a C++ generator as an
   explicit action. Both returned `21`; the input change regenerated dependent
   code and the application returned `22`. No-change rebuild reused cached actions.
   The unrelated native `#error` target was excluded.

The CMake generator was Python; the Bazel generator was C++. Both are allowed
pipeline implementations. These tests establish basic feasibility/isolation, not
an equal-language, equal-cache performance comparison. Sandbox restrictions affected
compiler/pip subprocesses; successful checks were rerun in the host environment.
Legacy runtime/build code was not modified by the spike.

### Not established

No local Bazel managed/NuGet/desktop build, LuaJIT build, production packaging,
IDE debugger session, remote cache, distributed build or large-monorepo benchmark
was completed in Q01. Source/upstream-CI observations are labeled separately from
executed tests. The decision is based on workflow ownership and validated SDK
integration, not a claimed speed ranking or a claim that Bazel cannot work.

## Dependency and setup consequences

- Reusable managed libraries can be independent `.csproj` package producers.
  Consume package boundaries using versioned NuGet `PackageReference`; coordinate
  local/CI pack -> restore -> build without publishing packages publicly.
- Python modules use `pyproject.toml`, wheels and recreatable build/runtime venvs.
  A pure-Python wheel packages code; native extensions require their own build step.
- A managed interpreter cache or a discovered compatible Python installation is
  preferable to checking interpreters/venvs into Git. Python binaries can live in
  ignored local tool directories or distributed tool artifacts when required.
- Plan a standalone, self-contained **C#/.NET 10 graphical environment manager**
  with a shared headless core. It detects Ready/Missing/Needs repair states and
  prepares selected profiles. It must not require Python or an installed .NET SDK
  merely to launch itself. See the [independent setup plan](../plans/environment-setup.md).
- Q02-Q07 and Y01-Y07 implement these policies in later individual tasks. Q01 does
  not add production presets, lockfiles, packaging scripts, venvs or setup UI.

## Trade-offs and reconsideration

The chosen approach has separate native, managed and Python graphs. The coordinator
must explicitly model artifact handoffs and selected-project impact; it must not
try to reproduce compiler-level dependency analysis. NuGet package boundaries add
version/pack/restore steps to cross-library changes. Recreating environments and
fingerprinting generators are real maintenance work, not automatic CMake features.

Reconsider Bazel in a new ADR if measured CI duplication/cache cost dominates,
cross-language action-level caching becomes a concrete requirement, or an accepted
rule/IDE/desktop path removes the current managed tooling cost. A future evaluation
must cover the actual selected UI framework, .NET 10 packaging/debugging, package
distribution, Python generation, Windows paths and clean-machine installation.
The size of a monorepo alone is not a reason to migrate.
