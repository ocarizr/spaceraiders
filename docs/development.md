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
tests. No external test framework or package download is required.

## Current coverage

`core.vector_coordinates` verifies the console collision-cell comparison used by
lasers, aliens, walls and power-ups. It covers positive and negative coordinates,
X/Y boundaries, truncation around zero, comparison symmetry and the arena corner.
These tests preserve the existing truncation behavior; they do not introduce a
new collision model. The test executable does not launch the interactive game.
Gameplay, object lifetime, timing and isolated-header checks remain later tasks.

## CI and the required check

[CI](../.github/workflows/ci.yml) runs for every pull request, pushes to `master`
and manual dispatch. Both `windows-debug` and `windows-release` configure, build
the game and tests, then run CTest on `windows-2022` with an x64 MSVC environment.
There are no caches or generated source prerequisites for this baseline.

The stable **CI gate** check depends on the native matrix and runs even when a
dependency fails. Only a `success` result passes; failure, cancellation or a skipped
native job fails the gate. A compilation failure stops the native job before CTest
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
