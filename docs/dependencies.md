# Managed package build and restore (Q05)

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
