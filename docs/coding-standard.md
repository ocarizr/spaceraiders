# Coding standard

The native library is **Raiders Engine**. **Space Raiders** is its sample game.
Repository code, comments, diagnostics and developer documentation use English.
Existing `raiders` namespaces and public schema identifiers remain stable.

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
