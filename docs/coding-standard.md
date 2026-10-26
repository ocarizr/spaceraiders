# Coding standard

The native library is **Raiders Engine**. **Space Raiders** is its sample game.
Repository code, comments, diagnostics and developer documentation use English.
Existing `raiders` namespaces and public schema identifiers remain stable.

## Architecture and completeness

Apply SOLID through clear responsibilities and composition at real variation
boundaries. Separate presentation, application commands, domain policy and external
effects. Reuse owned libraries/packages rather than copied source or loose binaries;
inject discovery, storage and process operations where tests need control. Use
patterns to solve a stated lifetime/extension/workflow problem, not to add layers
without a consumer. Keep interfaces small and ownership/cancellation explicit.

For every task, identify supported branches and real consumers before coding.
Unit fakes prove controlled behavior; they do not prove a production registration,
SDK payload or installed consumer. Missing data, adapters or validations require an
explicit backlog task and dependency. Update the plan and notify the maintainer
when discovered. Reopen unmet acceptance instead of silently reducing it. Follow
the [delivery evidence protocol](audits/2026-10-26-delivery-completeness.md#mandatory-delivery-and-discovery-protocol)
in progress updates and final handoffs, including the scope and limits of tests.

## Constants, resource tables and shared presentation

Human-facing tool text belongs in the shared localizable resource library: labels,
menus, accessibility text, dialogs and formatted diagnostics shown to users.
English is the baseline table, not an inline fallback repeated across views.
Use stable keys, typed parameters and documented missing-key/fallback behavior;
do not concatenate sentence fragments. Game-visible strings use localization keys
and bundle catalogs under the [localization plan](plans/localization.md).

Protocol fields, schema IDs, resource keys, enum wire values and diagnostic codes
are invariant identifiers: centralize them in typed contracts/constants or
authoritative schemas, and never translate them. Paths, URLs and version/hash
policy belong in reviewed configuration/manifests. Do not put every string in a
translation table or treat test inputs as shipped user interface resources.

Replace unexplained numeric literals with named constants/options with units and
rationale: timeouts, byte limits, capacities, geometry, spacing and typography.
Use the shared design tokens for visual dimensions/colors; use domain configuration
for tunable behavior. Basic loop indexes, identity values and explicit schema
examples need not become meaningless constants. Document framework-required values.

All interactive C# tools consume the shared UI/resource NuGet packages defined by
J09-J16. Applications supply view models, commands and event handlers; shared
components supply styling/accessibility/state behavior. Do not copy button/theme
implementations into each tool. J13/Y18 migrate the current setup; J15 adds focused
conformance checks. New code follows this rule immediately; existing literals are
tracked migration work rather than evidence that the rule is already enforced.

## Layout

- Use UTF-8, LF, a final newline and spaces. Use four spaces for source code,
  including CMake, Python, Lua and C#; use two for JSON and YAML.
- Aim for 100 columns. Formatters may retain indivisible strings or tokens.
- Use Allman braces in C++ and C#. Always brace control bodies, even one statement.
  Do not compress functions or control bodies onto one line.
- Leave a blank line between function definitions and after a complete control
  construct when another statement follows in the same scope.
- Keep related branches together: no extra separator between `if` and `else`,
  `try` and `catch`/`finally`, or the body and trailing condition of `do/while`.
  No separator is required immediately before the enclosing closing delimiter.
- Separate coherent logical groups with blank lines: input validation, preparation,
  execution and result handling, for example. Avoid a blank line after every
  statement. Reviewers judge these semantic boundaries; a formatter cannot infer them.
- Comments belong with the code they explain. Keep multiline strings unchanged.

```cpp
void Update()
{
    if (!ready)
    {
        return;
    }

    PrepareFrame();
    RenderFrame();
}

void Shutdown()
{
    ReleaseResources();
}
```

## Naming and structure

Keep existing public naming consistent within each module. For new code use
PascalCase for C++/C# types and methods, camelCase for native locals/parameters,
and snake_case for Python/Lua functions and locals. Use descriptive names instead
of type encodings. Keep CMake commands lowercase. Avoid unrelated renames while
formatting. Include headers rather than implementation `.cpp` files.

Keep engine, sample, editor and pipeline concerns in their project directories.
Tests belong to the project they exercise. Generated files, dependencies, build
outputs, managed interpreters and virtual environments are not handwritten source.

## Formatting tool

`tools/pipeline/code_style` is a Windows Python 3.12 package, installed from a
wheel. It runs clang-format 19.1.5 (C++/C#), Ruff 0.9.10 (Python), gersemi 0.22.1
(CMake), and StyLua 2.5.2 (Lua 5.1/LuaJIT syntax), followed by syntax-aware blank
line enforcement. Dependency versions are pinned in its `pyproject.toml`.
StyLua is downloaded explicitly and its archive SHA-256 is verified.

From the repository root in PowerShell:

```powershell
py -3.12 -m venv .venv/code-style
.venv/code-style/Scripts/python.exe -m pip wheel --no-deps --wheel-dir .tools/code-style/wheels tools/pipeline/code_style
.venv/code-style/Scripts/python.exe -m pip install .tools/code-style/wheels/raiders_code_style-0.1.0-py3-none-any.whl
.venv/code-style/Scripts/python.exe -m raiders_code_style --bootstrap-lua
.venv/code-style/Scripts/python.exe -m raiders_code_style --fix
.venv/code-style/Scripts/python.exe -m raiders_code_style
```

Rebuild and reinstall the wheel with `--force-reinstall` after changing the tool.
`--fix` formats selected files. Without it the command reports differences and
returns failure without writing source. With no paths it selects tracked changes
against HEAD plus untracked source. Explicit file paths are also accepted.
`--base <ref>` checks all source files changed since the merge base with that ref,
including local edits. Unsupported extensions are ignored. Generated/external
paths are refused. Syntax/parser errors fail the check rather than guessing.

`.editorconfig` supplies editor defaults; `.clang-format` supplies native/C#
formatting. Configure format-on-save using these same tool versions, then run the
repository command before staging. The command is the authoritative formatting
check; editor settings alone do not enforce blank lines after control blocks.

## Adoption and CI

CI checks changed source files and formatter regression fixtures on Windows.
The `CI gate` requires both native tests and formatting to succeed. Format each
source file touched by a change, including its pre-existing formatting. Adopt
legacy files in small batches rather than reformatting the repository at once.
This first batch covers the T04 trace writer, its tests and build declarations.
Generated code and third-party code are excluded from manual formatting.

Formatting checks do not replace compilation, language analyzers or review.
The check rejects C# control bodies without braces; add the braces and rerun it.
Logical grouping and naming clarity remain review rules. Dedicated .NET analyzers
are a later quality task.
