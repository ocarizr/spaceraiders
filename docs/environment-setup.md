# Environment setup profiles and health contract (Y01)

Version 1 defines the inputs and diagnostic results shared by the future Windows
setup GUI and headless doctor. This delivery contains schemas, synthetic examples
and conformance tests. The C# probe library is Y02, recipe execution is Y03,
self-contained GUI is Y04, interrupted-operation repair is Y05, headless commands
are Y06 and bootstrap distribution is Y07. No installer or workstation probe runs
as part of Y01 validation.

The application is C#/.NET 10, `net10.0-windows`, self-contained `win-x64`. Launch
requires no installed .NET SDK, Python, game executable, renderer, asset bundle or
Python build coordinator. Preparing a selected developer profile can install its
SDK later. The UI framework remains a Y04 choice. Windows x64 is the supported
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
