# Package build and restore

Reusable managed libraries are NuGet packages targeting .NET 10. SDK project
files remain the source of truth; this policy adds explicit package production
before consumer restore. It does not introduce a second build graph, copy loose
DLLs, or publish to a remote feed. Private implementation modules can retain
`ProjectReference` when they are not independent package boundaries.

## Select a consumer and its producers

Add a literal version to the nearest `Directory.Packages.props`, preserving the
root central-management settings. The root disables project-level version
overrides. Internal IDs use `Raiders.*`; ranges, wildcards and build metadata
are rejected. For example:

```xml
<PackageVersion Include="Raiders.Diagnostics" Version="0.1.0" />
```

The consumer selects both its NuGet dependency and the source project that must
produce it in this checkout:

```xml
<ItemGroup>
    <PackageReference Include="Raiders.Diagnostics" />
    <RaidersPackageProject Include="../Raiders.Diagnostics/Raiders.Diagnostics.csproj" />
</ItemGroup>
```

`RaidersPackageProject` locates a producer; it is not an assembly reference and
does not duplicate package ID/version metadata. Each producer's returned package
identity must match a direct `PackageReference` and central `PackageVersion` in
its consumer. Producers declare their own dependencies the same way. MSBuild
executes these edges transitively, finishing dependencies before packing their
consumers; a circular selection fails. Unrelated projects are never selected.
Multiple dependencies are currently processed sequentially; future orchestration
can schedule independent producers without changing NuGet consumption.

```powershell
# The path is an example for a future consumer, not an existing editor project.
dotnet msbuild tools/dotnet/MyTool/MyTool.csproj -t:RaidersRestore -p:Configuration=Release -p:RestoreLockedMode=true
dotnet build tools/dotnet/MyTool/MyTool.csproj --no-restore -c Release

# Existing standalone producer; no consumer or public publishing is required.
dotnet msbuild tools/dotnet/Raiders.Diagnostics/Raiders.Diagnostics.csproj -t:RaidersPackLocal -p:Configuration=Release -p:RaidersEnableProfiling=true -p:RestoreLockedMode=true
```

Check each exit code. `RaidersRestore` recursively packs the selected producers,
validates their identities, then restores the selected consumer. It does not
build that consumer. `RaidersPackLocal` first restores the library's package
dependencies, then builds and packs that library. Only packable libraries can be
selected as producers. Ordinary `dotnet restore` continues to restore existing
artifacts and does not implicitly build producers. Use the explicit targets for
local feed production; SDK `dotnet pack -o ...` does not enforce this publication
policy and must not write directly into the shared feed.

## Feed, versions and caches

`NuGet.config` maps `Raiders.*` specifically to ignored `out/packages`; other
dependencies use nuget.org. The shared restore target creates the local directory
before use. A more specific package-source mapping takes precedence over the
public wildcard. There are no credentials or push targets. An approved internal
server can replace the local source in a separately reviewed configuration.
[NuGet source mapping](https://learn.microsoft.com/en-us/nuget/consume-packages/package-source-mapping).

`RaidersLocalPackageFeed` overrides the producer destination. When overriding it,
also configure the consuming NuGet source to that same absolute path, using a
project-local `NuGet.config` or `RestoreConfigFile`; changing the property alone
does not redirect NuGet. Keep feeds/caches separated when testing unpublished
packages. Source mapping applies to downloads, not already cached packages.

Each pack operation builds a candidate under the producer's ignored
`obj/raiders-pack/<unique-id>`. Publication copies to a temporary file on the feed
volume and renames exclusively, so readers do not see partial packages. An
existing ID/version is never replaced. An identical logical payload succeeds
without touching the existing artifact. Comparison includes the nuspec and all
payload entries; ZIP timestamps/order and generated NuGet container metadata
(`_rels/.rels`, `[Content_Types].xml`, core-properties) are excluded. This is
payload equivalence, not a package signature or bit-for-bit rebuild guarantee.
Successful candidates are removed; failed candidates remain available for
inspection under `obj/`. Package validation here does not replace NuGet's
restore/content-hash checks.

If a library changes, assign a new version, update affected central pins and
regenerate/review the producer and consumer locks. Repack affected upstream
packages with new versions when their dependency metadata changes. Configuration,
profiling flags, SDK changes or other changes to payload bytes also require a
distinct version; do not produce Debug and Release variants under the same
ID/version. Use unique prerelease versions for iterative local development.

For an intentional graph update, run `RaidersRestore` with
`-p:RestoreLockedMode=false` (and `-p:RestoreForceEvaluate=true` when deliberately
reevaluating an existing lock), inspect every changed `packages.lock.json`, then
repeat with `-p:RestoreLockedMode=true`. Locked restore rejects missing locks and
graph changes. Commit reviewed versions/locks, not feed files or cache contents.
A literal NuGet version is a minimum constraint; the reviewed lock fixes the
actual resolution. Producer/pin matching additionally requires the selected
producer's exact literal version.

NuGet reuses packages by ID/version from its global packages folder. New immutable
versions change that cache key; changing source or clearing caches is not a
versioning strategy. Existing cached versions remain valid for old consumers.
[NuGet cache behavior](https://learn.microsoft.com/en-us/nuget/consume-packages/managing-the-global-packages-and-cache-folders).

## Validation and ownership

Build targets and the Roslyn MSBuild task live in `tools/dotnet/package_build`;
its MSTest project and committed lock are under `tests/`. The folder name avoids
the repository's ignored generated `build/` directories. Fixtures are generated
in ignored `out/tests/package-graph/<unique-id>` and retained for diagnosis.
They use an isolated feed/cache and no third-party fixture packages.

```powershell
dotnet restore tools/dotnet/package_build/tests/Raiders.Build.Tests.csproj --locked-mode
dotnet test tools/dotnet/package_build/tests/Raiders.Build.Tests.csproj --no-restore -c Release -- RunConfiguration.TreatNoTestsAsError=true
```

Tests execute MSBuild, restore and real consumer binaries to cover transitive
ordering, selected-only builds, absence of copied dependency DLLs in packages,
identity/version mismatch, cycle rejection, missing/changed locks, non-library
producers, floating versions, idempotence and immutable cache invalidation.
The existing managed CI job runs this suite and packs Raiders.Diagnostics through
the guarded target. CI gate continues to require that job. Selective job impact
classification remains O01-O03; this task does not implement it.

## Python wheel module and installed entry point (Q06)

`tools/pipeline/wheel_build` owns the `raiders-pipeline` distribution, the
`raiders_pipeline` namespace, tests and packaging metadata. The repository root
`pyproject.toml` stays an environment policy, not an installable distribution.
The package has no runtime dependencies. Its public API exports `build_wheel`,
`load_policy` and `__version__`; both `raiders-pipeline` and
`python -m raiders_pipeline` expose the same installed CLI.

The packaged `wheel_policy.json` selects setuptools 84.2.1 and wheel 0.48.0.
It is read using `importlib.resources`, without searching the checkout or current
directory. This first builder supports projects declaring those exact backend
requirements and `setuptools.build_meta`, without `backend-path` hooks. Other
backends/additional build requirements need an explicit policy extension.

Use the [Q07 recipes](#reproducible-python-environments-q07) below to prepare
separate Windows x64 Python 3.12 venvs and bootstrap the coordinator. Interpreters,
venvs, wheels and fixture projects remain ignored. Subsequent selected package
builds use the installed command. For example, from any working directory:

```powershell
D:/Projects/spaceraiders/.venv/python/tools/Scripts/python.exe -I -m raiders_pipeline build-wheel --project D:/Projects/spaceraiders/tools/pipeline/diagnostics --build-python D:/Projects/spaceraiders/.venv/python/build/Scripts/python.exe --wheel-dir D:/Projects/spaceraiders/out/python-wheels
```

Replace the example checkout paths with your own. Source is an explicit build
input; importing/running the installed coordinator does not require the source
checkout or its location. No `sys.path` modification, editable install or
`PYTHONPATH` workaround is used. The build interpreter must be a distinct venv
with the selected backend versions; the command rejects a global interpreter,
the execution venv, unsupported Python/platforms and missing backend pins.
The child uses Python isolated mode and pip's isolated configuration.

`build-wheel` builds exactly one selected project with `--no-deps`,
`--no-build-isolation` and `--no-index`. Build prerequisites are the caller's
responsibility; the command does not create environments or download packages.
Pip build diagnostics go to stderr; success emits one JSON object to stdout with
`schema_version`, absolute wheel path and SHA-256. Failure returns a nonzero code
and emits no success object. The temporary build-output directory is separate
from the requested artifact directory; existing wheels for other projects are
not included in the result. A wheel with the same filename at the destination is
replaced: explicitly reinstall the resulting artifact when testing new bytes.
This output directory is not an immutable package feed or runtime cache.
Runtime dependencies are installed separately by the consumer.

CI runs the pytest suite against the installed coordinator wheel. Tests exercise
its module and console entry points from an unrelated working directory, build
a real fixture wheel with package data, install it into a fresh third venv, move
the fixture source and run its installed entry points successfully. They also
cover backend/build failures, invalid projects/interpreters and environment
separation. The existing Code style job carries these checks into CI gate.

Locked environment creation and health checks are implemented below (Q07).
Whole-monorepo project orchestration is Q04, selective CI is O01-O03, and
native/binary Python wrappers remain their owning project tasks.

## Reproducible Python environments (Q07)

`raiders-pipeline` 0.2.0 packages two JSON recipes alongside its stdlib-only
`create_env.py` runner. They are developer/distribution inputs, not local state:

| Recipe | Installed graph |
| --- | --- |
| [python-build.lock](../tools/pipeline/wheel_build/src/raiders_pipeline/environments/python-build.lock) | pip, setuptools, wheel and packaging |
| [python-tools.lock](../tools/pipeline/wheel_build/src/raiders_pipeline/environments/python-tools.lock) | pip, pytest and its complete Windows dependency graph, plus the explicitly supplied raiders-pipeline 0.2.0 wheel |

Each external artifact has an exact version, wheel filename, official PyPI file
URL and SHA-256. The local package's identity/version is fixed in the tools
recipe; its wheel SHA-256 comes from the build/release manifest because a package
cannot contain the hash of its own complete wheel. CI calculates that hash after
building. A distributed SDK must supply the approved hash with its artifact.

Interpreter discovery checks an explicit `--python` first, or the running base
interpreter and Python candidates on PATH. It accepts only Windows x64 CPython
3.12.14 (Q02 development baseline) or 3.12.10 (Q02 hosted Windows baseline).
Store aliases are skipped. No compatible candidate is an actionable failure;
the runner does not silently change Python versions or install an interpreter.

For a machine without Python, the recipes also contain a native bootstrap
contract under `interpreter.bootstrap`: the official CPython 3.12.10 x64 installer
URL/SHA-256, per-user silent arguments, `{interpreter_root}` substitution,
executable path and accepted installer exit codes. The future C# setup manager
can read this JSON, verify/download/run the native installer and then invoke
the stdlib runner, without needing Python to read or execute the installer
contract. Interpreter installation/registration, restart handling (exit 3010),
distribution/licensing and GUI consent belong to Y02/Y03. Q07 verified the
downloaded installer against the digest in its official Sigstore bundle; it did
not install Python or perform Sigstore signature verification.
[Official release files](https://www.python.org/downloads/release/python-31210/).

From an existing compatible CPython checkout, check each exit code:

```powershell
$recipe = 'tools/pipeline/wheel_build/src/raiders_pipeline/create_env.py'
python -I $recipe create --profile build --root .venv/python --wheelhouse .tools/environments/wheels --download
.venv/python/build/Scripts/python.exe -I -m pip --isolated wheel --no-deps --no-build-isolation --no-index --wheel-dir .tools/pipeline/wheels tools/pipeline/wheel_build
$wheel = (Resolve-Path .tools/pipeline/wheels/raiders_pipeline-0.2.0-py3-none-any.whl).Path
$sha256 = (Get-FileHash -LiteralPath $wheel -Algorithm SHA256).Hash.ToLowerInvariant()
python -I $recipe create --profile tools --root .venv/python --wheelhouse .tools/environments/wheels --download --local-wheel $wheel --local-sha256 $sha256
.venv/python/tools/Scripts/python.exe -I -m raiders_pipeline env check --profile tools --root .venv/python --local-sha256 $sha256
```

The source runner deliberately needs no installed pipeline package or third-party
Python modules; its lock files travel beside it. Once installed, `raiders-pipeline
env ...` and `python -m raiders_pipeline env ...` use the same implementation and
bundled recipes. All paths are explicit or relative to the chosen managed root;
installed commands work outside the checkout. The graphical setup manager
remains a C# project in the Y plan, not a Python user interface.

`--download` fetches missing wheels only from locked URLs, verifies SHA-256 before
caching and removes failed partial downloads. Omit it for offline creation with
a prepared wheelhouse. Existing corrupt cache entries fail with their filename;
remove that specific artifact and explicitly download it again. All input hashes
are verified before replacing an environment. Pip then installs only those wheels
with `--no-index --no-deps --only-binary=:all: --require-hashes`; `pip check` and an
exact installed inventory check reject incomplete dependency graphs. The venv's
bootstrap pip is replaced by the locked pip wheel. No floating dependency
resolution occurs during recipe installation.
[Pip hash-checking mode](https://pip.pypa.io/en/stable/topics/secure-installs/).

`create` and `check` emit one JSON object on stdout; installation diagnostics go
to stderr. Results contain `profile`, `path` on normal checks, `status` and
`reasons`; operational errors can omit `path`. `Ready` returns zero; `Missing`
(no environment directory) or `Needs repair` return one. Argument errors return
two. `check` never creates or repairs anything. A healthy environment is reused
without needing cached wheels or the local artifact, although tools checks still
require the expected local SHA-256 to identify the desired release.

The ignored `.raiders-environment.json` receipt records the recipe fingerprint,
base interpreter identity, local wheel hash and installed distribution versions/
file fingerprints. Health rehashes distribution-owned files, including RECORD
and generated scripts, rather than trusting version strings or edited RECORD
hash columns alone. It detects changed/missing files, unexpected distributions,
recipe changes and disabled site isolation. Ephemeral `.pyc` files, arbitrary
unowned files and the standard library are outside this distribution fingerprint.
This checks installed package drift; it is not an operating-system integrity scan.

`create` repairs only an owned `<root>/build` or `<root>/tools` directory at its
original absolute path; venvs are not relocated. It rejects unowned directories,
linked targets, active/bootstrap environments and artifact inputs inside the
target. An OS file lock prevents concurrent creators and releases on process
exit. An incomplete creation retains a repairable receipt; rerunning `create`
can rebuild it after fixing the cause. A failed recreation may leave that
environment incomplete. Existing manually created Q06 venvs are not adopted or
deleted; use a fresh managed root such as `.venv/python`.

The installed pytest suite covers both profiles and all Q06 wheel/entry-point
acceptance, plus offline recreation, idempotence without a cache, drift/repair,
recipe changes, bad hashes/downloads, extra distributions, concurrent creators
and ownership/active-environment guards. CI bootstraps with the recipes, then
executes the suite from the installed tools environment:

```powershell
$env:RAIDERS_TEST_BUILD_PYTHON = (Resolve-Path .venv/python/build/Scripts/python.exe).Path
$env:RAIDERS_TEST_WHEELHOUSE = (Resolve-Path .tools/environments/wheels).Path
$env:RAIDERS_TEST_PIPELINE_WHEEL = $wheel
.venv/python/tools/Scripts/python.exe -m pytest -c tools/pipeline/wheel_build/pyproject.toml tools/pipeline/wheel_build/tests -v
```

The tools profile currently owns the wheel coordinator and its pytest suite;
formatter and diagnostics environments retain their existing project workflows.
Add profiles only when their owning work needs them. CI impact classification
continues to belong to O01-O03.
