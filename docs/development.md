# Native development and CI

Use Windows x64 and the MSVC/Ninja toolchain described in the
[README](../README.md#build-on-windows). Open an x64 Developer PowerShell for
Visual Studio 2022 and run from the repository root:

```powershell
cmake --preset windows-debug -DBUILD_TESTING=ON
cmake --build --preset windows-debug --parallel 2
ctest --test-dir out/build/windows-debug --output-on-failure --no-tests=error

cmake --preset windows-release -DBUILD_TESTING=ON
cmake --build --preset windows-release --parallel 2
ctest --test-dir out/build/windows-release --output-on-failure --no-tests=error
```

Stop on any nonzero command exit code. Build the default target so both the game
and tests compile; selecting only `SpaceRaiders` does not build the test executable.
CTest does not build tests automatically. `--no-tests=error` prevents an empty
test suite from passing silently. Test failures print diagnostics and return a
nonzero exit code in both Debug and Release.

CTest is enabled by default through `BUILD_TESTING`. To intentionally build only
the application, configure with `-DBUILD_TESTING=OFF`; re-enable it before running
tests. GoogleTest 1.17.0 is fetched with a SHA-256-verified archive only when
testing is enabled. Its MSVC runtime selection follows the parent project.
Add `-DRA_ENABLE_PROFILING=ON` when configuring to exercise the full profiling
and trace suites; configure with `OFF` to exercise their disabled contracts.
`-DRA_ENABLE_LOGGING=OFF` additionally exercises the disabled logging library.

## Test authoring standard

Use a maintained test framework for every language; do not implement a custom
assertion runner or a `main()` that manually counts failures.

| Language | Framework | Authoring and lifecycle |
| --- | --- | --- |
| C++20 | [GoogleTest](https://google.github.io/googletest/primer.html) | `TEST`, `TEST_F`, `TEST_P`, `SetUp`/`TearDown`, framework assertions |
| Python | [pytest](https://docs.pytest.org/en/stable/) | Plain test functions, `assert`, `pytest.raises`, fixtures, `pytest.mark.parametrize` |
| C# / .NET 10 | [MSTest](https://learn.microsoft.com/en-us/dotnet/core/testing/unit-testing-mstest-intro) | Test classes/methods, data rows, initialize/cleanup, framework assertions |
| LuaJIT | [Busted](https://github.com/lunarmodules/busted) | `describe`/`it`, assertions, before/after hooks, spies and mocks |

C++ and Python are integrated now. C# and Lua are the required conventions for
their first projects; pin their framework dependencies and add Windows runners
with those projects. No empty C#/Lua suites or placeholder CI jobs are installed.

Keep each test focused on a named behavior and preserve independence from order.
Use parametrization for the same contract over different inputs and fixtures for
shared setup and teardown. Prefer comparison assertions (`EXPECT_EQ`, `ASSERT_EQ`)
for useful value diagnostics. Use fatal assertions only when later operations
depend on the result. Do not hide failures in helper return codes or catch-all
blocks; unexpected exceptions must reach the framework. Join worker threads
before inspecting their results or destroying shared state.

Native test executables are framework hosts linked with `GTest::gtest_main`.
`gtest_discover_tests` registers individual cases with CTest, including parameters;
CTest remains the native orchestration entry point. The trace recovery suite has
a custom GoogleTest entry point solely to dispatch the intentionally terminated
crash child. This integration helper is not a separate assertion runner.

Keep test sources inside their owning project. `tests/core` temporarily owns
legacy root-source coverage until extraction of the engine core; diagnostics
tests remain in `engine/diagnostics/tests`. Temporary captures go inside the
ignored build directory. Fixtures remove successful captures and retain failed
captures for investigation. Tests must work in Release without relying on C
`assert` or `NDEBUG`.

## Python test pipeline

Build the formatter wheel using the setup in [coding-standard.md](coding-standard.md),
then install its pinned test extra and run the package suite:

```powershell
.venv/code-style/Scripts/python.exe -m pip install --force-reinstall '.tools/code-style/wheels/raiders_code_style-0.1.0-py3-none-any.whl[test]'
.venv/code-style/Scripts/python.exe -m pytest -c tools/pipeline/code_style/pyproject.toml tools/pipeline/code_style/tests -v --junitxml=out/test-results/code-style.xml
```

New Python packages own their test extras and pytest configuration. Run tests
against installed wheels instead of altering `PYTHONPATH` to bypass packaging.
Use pytest's `tmp_path` for temporary data and explicit fixture scopes. A failed
test or empty collection must fail the job; do not override pytest exit codes.

## Current coverage

`core.Coordinates/VectorCellTest` verifies the console collision-cell comparison used by
lasers, aliens, walls and power-ups. It covers positive and negative coordinates,
X/Y boundaries, truncation around zero, comparison symmetry and the arena corner.
These tests preserve the existing truncation behavior; they do not introduce a
new collision model. The test executable does not launch the interactive game.
Diagnostics suites cover structured logging, filtering/disabled macros, profiling
lifetimes and concurrency, trace export, bounded loss, I/O errors and crash recovery.
Gameplay and isolated-header checks remain later tasks.

## CI and the required check

[CI](../.github/workflows/ci.yml) runs for every pull request, pushes to `master`
and manual dispatch. Both `windows-debug` and `windows-release` configure, build
the game and tests with profiling ON and OFF, then run CTest on `windows-2022`
with an x64 MSVC environment. Native and Python runs produce JUnit XML in their
build/test output directories; CI logs also identify each individual test.
There are no caches or generated source prerequisites for this baseline.

The stable **CI gate** check depends on the native matrix and the code-style job
(including pytest), and runs even when a dependency fails. Only a `success` result
passes; failure, cancellation or a skipped
required job fails the gate. A compilation failure stops the native job before CTest
and therefore prevents a successful gate. The workflow has no path filters, so a
documentation-only pull request still receives the required check. Project impact
selection belongs to the later CI orchestration tasks.

After the first workflow run makes **CI gate** available, select it as a required
status check in the GitHub ruleset or branch protection for `master`. Workflow YAML
defines the check but does not activate repository merge rules. Keep code owner
approval enabled as well. See GitHub's guidance on
[dependent jobs](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idneeds)
and [required status checks](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches#require-status-checks-before-merging).

Changes to the CMake/test setup should be checked in both configurations. CI runner
tool updates can change compiler patch versions; production environment/version
pinning remains Q02. GitHub-hosted CI results must be reviewed on the pull request;
a local build alone does not establish that the hosted workflow passed.
