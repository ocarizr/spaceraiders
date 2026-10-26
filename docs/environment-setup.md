# Environment setup profiles, health and recipe execution

Version 1 defines the inputs and diagnostic results shared by the Windows setup
GUI and headless doctor. Y01 supplies schemas and conformance tests; Y02 supplies
the C# probe library. Y03 adds selected-profile recipe execution in
`Raiders.Environment.Core` 0.2.0. Y04 supplies the self-contained Windows setup
GUI. Interrupted-operation repair remains Y05, headless commands Y06 and bootstrap
distribution Y07. No real installer runs as part of automated validation.

The application is C#/.NET 10, `net10.0-windows`, self-contained `win-x64`. Launch
requires no installed .NET SDK, Python, game executable, renderer, asset bundle or
Python build coordinator. Preparing a selected developer profile can install its
SDK later. The GUI uses Windows Forms. Windows x64 is the supported
host; other architectures/platforms can be reported as unsupported, not prepared.

## Documents and ownership

| Document | Purpose |
| --- | --- |
| [Profile schema](../schemas/environment-profile.schema.json) | Versioned profile catalog, roles, target, version/configuration requirements and prerequisite IDs |
| [Health schema](../schemas/environment-health.schema.json) | Scan identity, selection, probe evidence, health, dependencies and sharing scope |
| [Profile example](../tests/integration/environment_setup/fixtures/environment-profile.json) | Five-profile catalog using existing Q02/Q05/Q07 policy inputs |
| [Health example](../tests/integration/environment_setup/fixtures/environment-health.json) | Synthetic shareable Ready report for the Python profile, not a measurement of this workstation |

Both schemas use JSON Schema draft 2020-12, reject undeclared properties and
require `schema_version: 1`. Readers reject unsupported document versions before
performing any preparation. New wire fields/states require a reviewed contract
revision; `revision` advances for changes to catalog policy. Enum strings are
case-sensitive, including `Needs repair`; C# bindings must preserve these wire
names rather than relying on an enum's default `ToString()` representation.
Resolve schema references locally; the `.invalid` schema IDs identify documents
and must never trigger network downloads. Validate date-time formats as well as
patterns; format annotation alone is insufficient.
[JSON Schema 2020-12](https://json-schema.org/draft/2020-12).

Y01 fixtures/tests live under `tests/integration/environment_setup` because they
join language-independent schemas with future C# probes and existing Python
recipe policy. They are not tests of the game or a production Python setup UI.
Machine paths, caches, receipts, logs and venvs stay ignored/local. The catalog
contains registry IDs, not shell commands, credentials or arbitrary installer URLs.

## Roles, selection and prerequisites

| Profile ID | Role | Required scope |
| --- | --- | --- |
| `native-development` | Native developer | MSVC compiler/toolset, Windows SDK, CMake and Ninja |
| `managed-tooling` | Tooling developer | .NET 10 SDK and the selected NuGet configuration/locked package closure |
| `python-pipeline` | Pipeline developer | Compatible interpreter and the Q07 build/tools environments |
| `lua-development` | Lua developer | Approved LuaJIT runtime/compiler artifact and VS Code Lua authoring/debug configuration |
| `designer-tools` | Designer | Distributed editor/game binaries and their manifest; Lua authoring tools are optional |

Roles group choices; they do not automatically select every profile in that role.
Prepare only explicitly selected profiles and their transitive `depends_on`
closure. The report records the original `selected_profiles`, exact
`resolved_profiles` closure and one profile result for every member. Do not scan
or prepare unrelated profiles merely because this is a monorepo.

Profile IDs are unique across the catalog. Requirement IDs are unique within a
profile; their `depends_on` references address requirements in that same profile.
Profile prerequisites address profile IDs. Both graphs must be acyclic, without
unknown/self references. Requirements marked required cannot depend on optional
requirements. Edges order preparation; independent read-only detection may still
run when a prerequisite is missing, or report Unknown if detection needs it.

The Designer closure must not require native build tools, .NET SDKs or Python
interpreters/venvs. It consumes prebuilt artifacts, including embedded runtimes,
and may offer an optional code editor. Keep compiler or generator extensions in
explicit developer profiles. Optional requirements are visible but do not block
Ready or trigger installation by default. An explicit additional authoring need
can select its companion profile; optional tooling is not a hidden dependency.

The example catalog's profiles currently have no cross-profile prerequisites.
The Python requirement graph orders interpreter -> build -> tools. This does not
prevent the Designer distribution from shipping prepared Python/native binaries
later without installing a developer Python profile.

## Constraints and approved inputs

Architecture applies to the selected tool/runtime, not just the OS. `x64` requires
an x64 candidate; `any` is reserved for architecture-independent requirements such
as feed configuration. Directory existence, PATH presence or a product's display
name alone never proves the executable's architecture or version.

| Constraint kind | Required interpretation |
| --- | --- |
| `exact` | Match the full version string, including a suffix; do not equate compiler, toolset and IDE versions |
| `one-of` | Match one explicitly approved version, without arbitrary patch upgrades |
| `sdk-feature-band` | Follow `global.json`: same .NET major/minor/feature band, at least the requested patch, `latestPatch`, no prereleases |
| `configuration` | Resolve the registry ID and verify its current configuration/recipe fingerprint plus operational checks |
| `manifest` | Resolve an approved artifact/configuration manifest, then verify its pinned identity, architecture, version and hashes |

For SDK baseline `10.0.402`, `10.0.402` and `10.0.450` satisfy the version rule;
`10.0.401`, `10.0.500`, `11.0.402` and prerelease SDKs do not. A wrong installed SDK
on a supported machine needs repair; it does not make Windows unsupported.
Native exact values in the fixture reproduce the verified Q02 baseline rather
than declaring an unreviewed new servicing/roll-forward policy.

Registry mappings for this first catalog are:

| Reference | Policy source / owner |
| --- | --- |
| `native-toolchain-v1`, `native-build-tools-v1` | [Build contract](build-system.md) native baseline; detection/acquisition adapters remain Y02/Y03 |
| `dotnet-sdk-v1` | Root `global.json`; SDK acquisition adapter remains Y03 |
| `nuget-config-v1`, `nuget-restore-v1` | `NuGet.config`, central versions/locks and [Q05 policy](dependencies.md#select-a-consumer-and-its-producers) |
| `python-interpreter-v1` | Q07 interpreter pins and native bootstrap metadata |
| `python-build-v1`, `python-tools-v1` | Bundled Q07 JSON locks, local wheel release hash and fresh receipt/file checks |
| `luajit-development-v1`, `vscode-lua-v1` | Future approved scripting/debugger manifests, owned by G tasks |
| `designer-sdk-v1` | Future Designer distribution manifest, owned by X/P tasks |

Some references deliberately have no implemented installer or published artifact
yet. An unresolved constraint manifest yields Unknown/RecipeUnavailable, never
Ready, an invented version pin or evidence of missing software. A missing
preparation recipe does not invalidate otherwise conclusive detection, but must
disable automatic preparation and explain why no action is available.

Feed detection distinguishes a complete, verified offline package closure from
an inconclusive network failure. A timeout yields Unknown/ProbeTimedOut, not
Missing. Q05 package/version/lock mismatches and Q07 fingerprint drift are repair
conditions. No profile scan triggers asset cooking or game builds.

## Evidence and health states

Every report includes session/scan IDs, the catalog ID/fingerprint, UTC start/end,
scan completion, machine OS/architecture and the selected/resolved profiles.
Every resolved profile reports every declared requirement, including optional or
unchecked ones; omitting failed probes cannot make a report Ready. Prerequisite
profile states must match their entries in the same resolved report.

Evidence contains the policy's `probe_id`, outcome, determination flag,
observation time, detected version/architecture, configuration/manifest fingerprint
and a typed location. `complete` means a conclusive observation; a process that
exited with an error is not automatically conclusive. Determinate outcomes
(`satisfied`, `absent`, `mismatch`, `unsupported`) require a UTC observation time.
Incomplete outcomes are `failed`, `not-run` or `running`.

| State | Meaning and evidence | Preparation behavior |
| --- | --- | --- |
| `Ready` | All required checks/prerequisites satisfied by fresh complete evidence; no unresolved restart or privilege action | No reinstall |
| `Missing` | Complete search proves a required tool or selected managed environment is absent | Offer acquisition of that requirement/closure |
| `Needs repair` | Complete evidence proves wrong version/architecture, stale fingerprints, incomplete setup or a pending prerequisite action | Explain failed requirement and propose a specific repair |
| `Unsupported` | The host cannot satisfy this profile's supported platform/architecture | Explain unsupported target; do not install |
| `Checking` | The current scan is still running | Show partial results; do not label absence |
| `Unknown` | Not checked, interrupted, failed/timed-out probe or unresolved expected policy | Retry/inspect; do not infer Missing or Ready |

Ready assessments require at least one satisfied, complete evidence record and
no reason codes. Missing requires an `absent` record; Needs repair a `mismatch`;
Unsupported an `unsupported` record. Other states require reason codes but may
have no evidence when not checked. Evidence marked satisfied but incomplete
cannot claim success. Codes such as `RestartRequired`, `PrivilegeRequired` and
`IncompleteSetup` explain repair conditions rather than silently treating a
successful installer exit as Ready. No raw installer output is embedded here.

For a supported, identified host, aggregate required requirement states and direct
prerequisite profile states in this priority order:

`Unsupported` > `Needs repair` > `Missing` > `Checking` > `Unknown` > `Ready`.

Apply this recursively through the prerequisite DAG; aggregate resolved profiles
the same way for the report. Optional states do not affect aggregation. A
confirmed problem remains visible while another probe is unknown. A missing
prerequisite adds `DependencyNotReady` and its state; do not fabricate a missing
installation in an otherwise healthy consumer. An unsupported host overrides
profile states. Unknown host architecture yields Checking while scanning, then
Unknown if still inconclusive. Report Ready additionally requires a finished scan
and Windows x64 eligibility. Finished scans can still be Unknown.

`scan_complete` means scheduled probes finished or produced terminal diagnostic
results, not that the environment is healthy. `completed_at` is null until then.
UTC timestamps are observation metadata, not CPU/profiling durations. T01
monotonic clock domains remain the telemetry timing contract.

Bindings must also enforce semantic rules JSON Schema cannot express: unique IDs,
known/acyclic prerequisites, exact closure and requirement inventories, unchanged
required flags, matching probes, cross-entry dependencies, evidence that satisfies
the resolved constraint, and correct aggregation. Validate before offering actions.
The tests exercise these invariants for the synthetic examples; Y02 must apply
them to real observations. The fixture's zero-valued fingerprints are synthetic,
not trusted hashes for any real environment.

## Freshness, sharing and Python integration

Catalog fingerprints are SHA-256 of UTF-8 source text after removing an optional
BOM and normalizing CRLF/lone CR to LF. Other whitespace and member order remain
significant. This makes the fixture fingerprint stable across Git newline
conversion. Installer, package and executable hashes remain hashes of raw bytes;
never normalize binary artifacts. Changes to referenced locks/manifests also
invalidate prior constraint evidence even if the catalog text is unchanged.

A saved Ready result is historical evidence, not authorization to skip detection.
Reopening/rechecking a profile reruns relevant probes and validates current
versions/fingerprints. Q07's health runner is an existing Python requirement probe
input, not a full Y01 report. Y02 maps its build/tools results to the Python
requirements, wraps them with fresh observation/session/catalog metadata and
redacts paths. A missing exact managed slot can be conclusive absence; a failed
global tool search is not. The setup application discovers/acquires Python only
after launch when a selected profile needs it; see the [Q07 bootstrap contract](dependencies.md#reproducible-python-environments-q07).

`visibility: local` permits `local-path` evidence for the UI. Shareable reports
permit only `managed-relative` locations or `redacted` with a null value. Relative
locations never contain traversal/drive roots. There are no fields for hostname,
user identity, credentials, environment dumps, raw commands, stdout/stderr or free
exception text. Producers must sanitize observed values too; schema validation
alone is not a secret scrubber. Share/export is an explicit user action.
Structured logs/profiling use T01/T05 correlation IDs separately from the JSON
result stream and follow the same redaction policy.

## Validation

Tests use pytest and a hash-pinned JSON Schema validator with an offline registry;
UTC format checking is explicitly enabled. They read fixtures and policy files
only, without executing any environment probe or installer. CI runs them within
the existing Code style job; impact selection remains O01-O03.

```powershell
python -m venv .venv/environment-contract
.venv/environment-contract/Scripts/python.exe -m pip install --only-binary=:all: --no-deps --require-hashes -r tests/integration/environment_setup/requirements.lock
.venv/environment-contract/Scripts/python.exe -m pytest -o strict_config=true -o strict_markers=true tests/integration/environment_setup -v
```

Check each command's exit code. The test dependency lock targets CPython 3.12 on
Windows x64 and is not a runtime dependency of the future C# application.

## Selected-profile preparation (Y03)

The reusable .NET 10 library lives in
[`tools/dotnet/Raiders.Environment.Core`](../tools/dotnet/Raiders.Environment.Core).
`RecipeRunner` shares Y02's `IToolDiscovery` and `EnvironmentProbe`, accepts trusted
`SetupRecipe` registrations, and prepares only required requirements in the
selected profile closure. Optional tools remain visible in health results but
are not installed implicitly. Both profile and requirement prerequisites run
before consumers, regardless of declaration order. Independent branches can
continue after failure; a failed or unresolved prerequisite blocks its consumers.
Execution is sequential, with one run at a time per runner instance. The future
V tasks own parallel scheduling; hosts must also serialize separate runners that
would modify the same environment.

Each registration binds a profile ID, requirement ID, `recipe_ref` and
`SetupRecipe.Fingerprint(requirement)`. A changed requirement policy blocks an old
registration with `PolicyChanged`. The application supplies reviewed artifacts
and actions separately from the catalog: the JSON never becomes shell commands,
installer arguments or arbitrary download URLs. Missing registrations remain
unavailable; Y03 does not invent release manifests for native/Lua/Designer tools.
Acquisition/execution adapters are provided, while product-specific pins and
registrations remain explicit inputs to the host.

The runner always probes current state. `Ready` requirements are no-ops and do not
create a cache, acquire artifacts or execute an action. Only `Missing` or
`Needs repair` observations authorize a registered recipe; inconclusive detection
blocks execution. Every action, including one reporting failure, is followed by
a fresh health scan. Exit zero alone never means success. A final scan captures
the resulting closure, and `EnvironmentState.Status` also preserves failed or
blocked actions even if a later observation happens to be healthy.

`ArtifactAcquisition` takes an absolute caller-owned cache directory and approved
`SetupArtifact` descriptors: an ID, HTTPS or local-file source, leaf filename,
SHA-256 and maximum byte size. Downloads stream to unique partial files, enforce
the size bound and raw-byte hash, then publish atomically without overwriting an
existing artifact. Cached bytes are rehashed before reuse, including offline.
Corrupt cached entries are rejected. Failed/cancelled transfers remove their own
partial files. Local-file sources support offline bundles; UNC sources, embedded
URL credentials, traversal filenames and cache links are rejected. Cache files,
installers and machine state stay outside Git. Cache acquisition never extracts
archives, installs software or removes an existing environment.

Actions receive a `RecipeContext` containing only that recipe's acquired artifacts.
`RecipeProcess.ExecuteAsync` runs an explicit absolute executable with separate
arguments, no shell, a bounded timeout and caller cancellation. Child stdout/stderr
are drained with a size bound and discarded. Cancellation/timeout terminates the
owned process tree. Code 0 means execution completed; 3010/1641 require restart,
740 requires privileges, and other codes fail. Launch access/elevation failures
also require privileges. Additional installer-specific codes must be interpreted
by a reviewed adapter rather than guessed as success.

Restart/privilege outcomes remain `Needs repair` in the Y01 health report, even
when a tool version probe succeeds. They block dependent installation and repeated
runs on the same runner. Y03 supplied in-memory session state; the production
backend now opts into Y05's durable repair and explicit action reconciliation
described below. Neither mode interprets an old Ready report as proof or claims
rollback of arbitrary third-party installer side effects.

The existing Q07 CPython locks supply approved native bootstrap URL/hash/arguments
for a Python interpreter recipe. A host can register that installer action and
then register the Q07 build/tools `create` operations behind their requirement
dependencies, using the reviewed runner/locks and wheel release hash. Q06's
installed wheel builder/CLI can be invoked through the same process adapter once
its environments exist. Approve scripts/locks as a versioned unit and pass paths
explicitly; no PATH-only interpreter selection, unverified checkout script,
unconditional network restore or new Python packaging policy is introduced by
this library. Preparing the selected Python profile can acquire Python; launching
the future C# setup application still does not require it.

`IProgress<PreparationProgress>` reports checking/acquiring/executing/validating,
step counts, completion and cancellation under one run ID. These are phase/count
updates, not invented download percentages. Caller cancellation propagates as
`OperationCanceledException`; inspect health again before resuming. Synchronous
observer failures cannot skip installation validation. T05 logs contain stable
IDs, status and elapsed microseconds, without paths, URLs, child output or raw
exceptions; planning scopes follow `RaidersEnableProfiling`. Health remains
shareable/redacted by default. GUI and headless callers use the same runner.

### Managed validation

MSTest fixtures under the library's `tests/` directory use isolated caches, HTTP
stubs and fake installers. A small PowerShell child writes only fixture files;
tests never run a real SDK/interpreter installer or mutate developer environments.
The existing managed CI step discovers these tests in both build configurations
and profiling modes and packs the versioned NuGet; no extra pipeline is needed.

```powershell
dotnet restore tools/dotnet/Raiders.Environment.Core/tests/Raiders.Environment.Tests.csproj --locked-mode
dotnet test tools/dotnet/Raiders.Environment.Core/tests/Raiders.Environment.Tests.csproj --no-restore -c Release -p:RaidersEnableProfiling=true -- RunConfiguration.TreatNoTestsAsError=true
dotnet msbuild tools/dotnet/Raiders.Environment.Core/Raiders.Environment.Core.csproj -t:RaidersPackLocal -p:Configuration=Release -p:RaidersEnableProfiling=true -p:RestoreLockedMode=true
```

Check every command's exit code. Machines retaining a different 0.2.0 development
package must use an isolated feed for local validation; immutable published
versions must not be overwritten. The package retains its versioned
`Raiders.Diagnostics` dependency; test assemblies and generated state are excluded.

## Windows setup application (Y04)

[`Raiders.Environment.Setup`](../tools/dotnet/Raiders.Environment.Setup) is a
Windows Forms `net10.0-windows` application, published for `win-x64` with its .NET
runtime and native runtime libraries bundled. Windows Forms fits this
Windows-only utility and permits a small code-based UI without generated XAML or
designer files. Trimming is disabled. DPI scaling uses PerMonitorV2.
[Windows Forms](https://learn.microsoft.com/en-us/dotnet/desktop/winforms/overview/),
[single-file deployment](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview).

To use a published build, open `Raiders.Environment.Setup.exe`. No SDK, Python,
game executable, source checkout or current-directory layout is needed to open
the window. The application starts without scanning or installing anything.
The production profile catalog is compiled from reviewed Y01/Q02 requirements;
`global.json` and Q07's Python lock are embedded policy inputs. Tests compare the
requirements against the reviewed Y01 catalog. No test fixture is a production
runtime dependency. Source and binaries remain separate; publishing into ignored
`out/setup/win-x64` does not add executables to Git.

1. Check the profiles needed for your role. Unselected profiles are not scanned.
2. Click **Scan selected**. Required profile prerequisites are included by the core.
3. Review component states and actions. Selecting a row shows required/detected
   versions, architecture, observation time, configured location and reasons.
   Optional components are labeled and are not installed implicitly.
4. Click **Prepare available actions** and review the confirmation, including the
   proposed CPython version and per-user target when applicable. Only registered
   recipes can execute; unavailable components remain unresolved.
5. Observe phase/step progress or cancel. Closing a busy window requests
   cancellation and waits for the operation before disposing controls. Re-scan
   after cancellation or failure. Healthy components do not reinstall.

Selection is locked during work. Changing it invalidates old health and action
availability; preparation requires a fresh scan. Probes/recipes run off the UI
thread, using the same `EnvironmentProbe` and `RecipeRunner` as other consumers.
The UI shows real failures and pending restart/privilege actions, even when a
subsequent version check succeeds. Pending actions are retained across selection
changes during the current app session. Y05 adds durable interruption evidence and
explicit installer-action reconciliation as described below; the Y04 session-only
behavior is retained for legacy test hosts. Y06 adds the headless entry point
below; bootstrap/release acquisition remains Y07.

### Registered capabilities in this delivery

The default host probes the .NET SDK from its standard installation/PATH, Python
from its managed/per-user/PATH locations, and CMake/Ninja executables on PATH.
Store execution aliases are excluded from Python candidate discovery. Detection
requires actual version/architecture checks, not executable names alone. A missing
configured interpreter slot can be conclusive; unresolved manifests remain
Unknown, with preparation unavailable.

The approved Q07 CPython bootstrap is registered: it downloads the pinned official
installer, verifies SHA-256, and uses Q07's existing per-user arguments without
changing PATH or requiring Python to run the installer. Its target is
`%LOCALAPPDATA%/Raiders/Setup/python312`. Download cache and structured logs also
live under this per-user Setup directory. Exception text, child output and URLs
do not enter the UI log stream. UI operations log their duration separately from
the core's optional profiling scopes.

The GUI does not fabricate MSVC/SDK, NuGet-closure, Python-venv, Lua or Designer
distribution manifests/recipes. These components are displayed as unavailable
until reviewed bindings/payloads are supplied through `ISetupBackend`; installing
an interpreter alone does not claim that the entire Python pipeline is ready.
The UI/backend seam permits the same workflow to exercise approved recipes and
isolated test installers without duplicating environment preparation logic.

### Build, publish and test

Building requires the pinned .NET 10 SDK on Windows. Running the published
application does not require a developer runtime or SDK.

```powershell
$tests = 'tools/dotnet/Raiders.Environment.Setup/tests/Raiders.Environment.Setup.Tests.csproj'
dotnet restore $tests --locked-mode
dotnet publish tools/dotnet/Raiders.Environment.Setup/Raiders.Environment.Setup.csproj --no-restore -c Release -p:RaidersEnableProfiling=true -o out/setup/win-x64
$env:RAIDERS_SETUP_PUBLISHED_EXE = (Resolve-Path out/setup/win-x64/Raiders.Environment.Setup.exe).Path
dotnet test $tests --no-restore -c Release -p:RaidersEnableProfiling=true -- RunConfiguration.TreatNoTestsAsError=true
```

Check each command's exit code. The entry point and window share `SetupWindow.cs`;
UI state and the default policy/backend composition live in `ProfileViewModel.cs`.
The app and its owned MSTest project have reviewed package locks. The existing
managed CI job publishes first, then runs all UI tests with the published path.

Tests cover selection, health/evidence binding, unavailable actions, no-op health,
failure reporting, pending actions, UI-thread progress/cancellation and close
lifecycle. They use the real WinForms controls with fake probes/installers; no
real installer executes. The publish test launches the actual EXE from an
unrelated working directory with empty PATH and nonexistent .NET/Python roots,
inspects the owned hidden window, and verifies the host's self-contained-runtime
trace. It closes the process cleanly. Optional `RAIDERS_SETUP_PREVIEW` and
`RAIDERS_SETUP_CATALOG_OUTPUT` paths capture a fixture-window PNG and production
catalog for local visual/schema validation; these outputs stay ignored.

## Durable repair and interrupted setup (Y05)

The production backend passes an owned `RepairPlan` directory to `RecipeRunner`:
`%LOCALAPPDATA%/Raiders/Setup/repair`. A versioned `repair-state.json` records the
catalog/profile/requirement identity, requirement policy SHA-256, last preparation
phase and failure/pending reason. It stores no credentials, executable arguments,
installation paths or Ready receipts. It remains machine-local and outside Git.

Before acquisition/execution/validation, the runner flushes an atomic replacement
of this journal. A crash or cancellation leaves interruption evidence for the next
instance. Exclusive `repair.lock` access prevents separate setup processes from
modifying the same state or executing installations concurrently. Closing/crashing
releases the OS handle; a leftover lock filename does not mean the tool is busy.
Invalid, oversized or unsupported journal data fails closed and is preserved for
diagnosis. Linked/reparse-point storage is rejected. Do not delete the repair record
to bypass installer restart requirements; retain it when investigating corruption.

`RecipeRunner.ScanAsync` combines fresh probes with recorded interruption evidence.
Incomplete steps report Needs repair (`IncompleteSetup`); Unknown, Checking and
Unsupported probes are never promoted to repair authorization. Requirement version,
package/configuration and venv fingerprints still come from actual probes, not the
journal. Changing policy does not authorize an outdated recipe: execution rechecks
the current registration fingerprint. Y08-Y13 still own missing production bindings.

After reviewing available actions, **Prepare available actions** rechecks each step.
Already healthy prerequisites are skipped. An interrupted step that now passes all
current probes can finish by validation alone; otherwise only its approved repair
recipe runs. Failure blocks dependent steps and remains visible on reopening.
The runner never removes environment directories, source files, user settings or
unrelated toolchains. It does not promise rollback of arbitrary third-party installer
side effects; recipe authors must preserve their last-good managed environment and
respect ownership when replacing it. New production venv bindings remain Y11.

Artifact acquisition reuses only hash-verified cached files. During an explicit
preparation retry, a corrupt cached artifact is replaced only after a complete
replacement passes SHA-256 and size checks; failed/offline acquisition preserves
the existing destination. Incomplete `.partial` files are never accepted as artifacts;
retries use new isolated temporary files. The runner does not broadly delete old
partials because another acquisition might own them. Direct `AcquireAsync` callers
retain strict corrupt-cache rejection unless `repairCorruptCache: true` is supplied.
For offline use, provide the approved artifact in the managed hash/file cache slot;
unverified or incomplete data never authorizes execution. A failed acquisition shows
connectivity/offline guidance in component details, then allows a reviewed retry.

RestartRequired and PrivilegeRequired remain blocking across application sessions,
even when files already look healthy. Complete the external action, then select
**Recheck installer action** and explicitly confirm it. The shared core performs a
fresh scan with `acknowledgePendingActions: true`; only conclusively checked selected
components are reconciled. Ready checks clear the pending state; Missing/Needs repair
checks allow a normal reviewed retry. Inconclusive checks retain the pending action.
Confirmation does not reboot Windows, grant privileges or execute an installer.
This is user acknowledgement plus probe validation, not automatic reboot detection.

MSTest cases under `Raiders.Environment.Core/tests/RepairTests.cs` exercise interrupted
phases, cancellation, persisted installer actions, failed dependencies, fingerprint
drift, no-op reopen, state corruption, cross-instance locking and verified cache
recovery with offline failures. All use isolated fake installers/local artifacts;
no real SDK installation is performed. The existing WinForms/published EXE tests
continue to validate the GUI and runtime-independent startup.

## Headless setup and doctor (Y06)

The same self-contained `Raiders.Environment.Setup.exe` opens the GUI with no
arguments. Any arguments route to a headless command **before** WinForms
initialization; malformed commands return errors without dialogs. Both paths use
`WindowsSetupBackend`, the same approved catalog/probes/recipes and Y05 repair
store. Headless mode does not add production toolchain bindings that are still
planned in Y08-Y13.

| Command | Behavior |
| --- | --- |
| `--help` or `help` | Print JSON command syntax without constructing a backend or creating setup storage |
| `profiles` | List profile IDs/titles/dependencies; this does not scan or install tools |
| `doctor --profile ID` | Fresh selected-profile scan, including required dependencies and persisted interruption evidence |
| `prepare --profile ID --yes` | Explicit consent to prepare the selected required closure using registered recipes and fresh checks |

Repeat `--profile ID` to select multiple profiles; duplicates are coalesced. No
implicit all-profiles selection is allowed. `prepare` without `--yes` is an error
and performs no scan/acquisition/installation. Unknown profiles, options or
incompatible flag combinations are errors; commands never come from a supplied
catalog or arbitrary executable argument.

Optional `--state-root ABSOLUTE_PATH` selects an owned cache/logical repair root;
otherwise the GUI's `%LOCALAPPDATA%/Raiders/Setup` is used. This flag does not
redirect tool installation to a workspace by itself; each approved recipe still
owns its documented target. `--timeout-seconds N` sets the command deadline
(1-7200 seconds; default 300). Cancellation/deadline expiration returns a structured
result; the shared runner retains interruption evidence and drains/kills its owned
installer process according to the existing Y03 cancellation contract. Ctrl+C is
handled when the host supplies a console; CI should use the explicit deadline.

After completing a required restart/privilege action, use
`doctor --profile ID --acknowledge-actions` to invoke Y05's explicit reconciliation.
It does not grant privilege, reboot or install anything. `prepare` does not accept
that flag, and `doctor` does not accept `--yes`. Failed/inconclusive checks remain
visible. A prior Ready result is never accepted as proof of current health.

Stdout contains exactly one UTF-8 JSON document and a newline. The envelope's
`schema_version` is 1; its contract is
[`environment-command.schema.json`](../schemas/environment-command.schema.json).
Nested `health` uses the existing Y01 schema with `visibility: shareable` and all
evidence locations redacted. `steps` contains typed preparation outcomes; it is
null for non-preparation commands. Other unused result fields are explicitly null.
Stderr is a separate bounded structured log stream with session IDs and preparation
progress. Optional profiling uses the existing diagnostics build switch; no human
log text, raw exception, command arguments or local tool path enters stdout.

| Exit code | Status | Meaning |
| --- | --- | --- |
| 0 | Completed | Help/list succeeded, or all selected required health checks are Ready |
| 2 | Blocked | Health is not Ready, requirements are unavailable or an installer action is pending |
| 3 | Failed | At least one preparation step failed |
| 64 | InvalidArguments | Invalid syntax, missing explicit consent or unknown selection |
| 70 | Error | Backend, storage or internal operation failed; diagnostics are sanitized |
| 130 | Cancelled | Caller cancellation or command deadline expired |

Use exit codes and structured health together. In particular, a successfully
executed doctor that finds Missing/Unknown tools returns 2; it is not a failed
JSON protocol invocation. Unknown does not authorize installation.

Because the application is a Windows GUI-subsystem executable, explicitly wait
for it and redirect its handles in PowerShell automation:

```powershell
$setup = (Resolve-Path out/setup/win-x64/Raiders.Environment.Setup.exe).Path
$process = Start-Process -FilePath $setup -ArgumentList @('doctor', '--profile', 'managed-tooling') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput doctor.json -RedirectStandardError doctor.log.jsonl
$result = Get-Content -Raw doctor.json | ConvertFrom-Json
$process.ExitCode
$result.health
```

Run a reviewed preparation by replacing arguments with
`@('prepare', '--profile', 'python-pipeline', '--yes')`. This may install the
approved Python interpreter; it does not claim that the currently unbound venv
requirements are Ready. The default managed profile detects the SDK but still
lacks Y10's approved SDK installation/NuGet closure binding.

A prebuilt self-contained artifact includes its .NET runtime: GUI/headless launch
does not require .NET SDK or Python on the destination machine. Building this
application from source does require the pinned SDK. Y07 owns verified bootstrap
artifact acquisition/distribution; Y10 owns installing the development SDK from
setup. Those are separate from the runtime bundled in this executable.

The existing managed CI job publishes first, runs GUI and headless MSTest cases,
then captures a real doctor JSON result with isolated state. The subprocess tests
also run help/list/doctor/invalid input from an unrelated directory with empty PATH
and absent .NET/Python roots. Tests verify consent, selected closure/idempotence,
live drift, pending-action reconciliation, redaction, separate logs, failures and
deadlines. No real installer is executed by this validation.
