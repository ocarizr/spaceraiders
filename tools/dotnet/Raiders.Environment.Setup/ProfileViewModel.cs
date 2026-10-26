using System.Reflection;
using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Raiders.Diagnostics;
using Raiders.Environment.Core;

namespace Raiders.Environment.Setup;

public interface ISetupBackend
{
    string CatalogJson { get; }
    bool SupportsRepair => false;
    bool HasRecipe(string profileId, ToolRequirement requirement);

    string DescribeAction(string profileId, ToolRequirement requirement)
    {
        return "";
    }

    Task<EnvironmentHealth> ScanAsync(string[] selected, CancellationToken token);

    Task<EnvironmentHealth> ReconcileAsync(string[] selected, CancellationToken token)
    {
        throw new NotSupportedException("This backend does not support durable repair.");
    }

    Task<EnvironmentState> PrepareAsync(string[] selected, IProgress<PreparationProgress> progress,
                                        CancellationToken token);
}

public sealed record SetupAction(string Profile, string Requirement, string State, string Action,
                                 string Details);

/// <summary>UI state and commands; detection and preparation remain in the shared core.</summary>
public sealed class ProfileViewModel(ISetupBackend backend, Log? log = null) : IDisposable
{
    private readonly HashSet<string> selected = [];
    private readonly Dictionary<(string Profile, string Requirement), RecipeStepResult> pending =
        new();
    private CancellationTokenSource? operation;
    private EnvironmentHealth? health;
    private EnvironmentState? preparation;
    private PreparationProgress? progress;
    private string message = "Select the environments you need, then scan their current state.";
    private long generation;

    public event EventHandler? Changed;
    public ProfileCatalog Catalog { get; } = EnvironmentProbe.ReadCatalog(backend.CatalogJson);
    public bool IsBusy => operation is not null;
    public EnvironmentHealth? Health => health;
    public EnvironmentState? Preparation => preparation;
    public PreparationProgress? Progress => progress;
    public string Message => message;
    public string[] Selected => Catalog.Profiles.Where(profile => selected.Contains(profile.Id))
                                    .Select(profile => profile.Id)
                                    .ToArray();
    public bool CanScan => !IsBusy && selected.Count > 0;
    public bool CanPrepare => CanScan && health is not null && !HasPendingAction &&
                              Actions.Any(action => action.Action is "Install" or "Repair");
    public bool HasPendingAction =>
        pending.Keys.Any(key => (health?.ResolvedProfiles ?? Selected).Contains(key.Profile)) ||
        health?.ReasonCodes.Any(reason => reason is "RestartRequired" or "PrivilegeRequired") ==
            true;
    public bool CanReconcile =>
        CanScan && health is not null && backend.SupportsRepair && HasPendingAction;

    public IReadOnlyList<SetupAction> Actions
    {
        get {
            if (health is null)
            {
                return [];
            }

            return health.Profiles
                .SelectMany(
                    profile => profile.Requirements.Select(
                        assessment =>
                        {
                            var policy = Catalog.Profiles.Single(item => item.Id == profile.Id)
                                             .Requirements.Single(item => item.Id == assessment.Id);
                            pending.TryGetValue((profile.Id, assessment.Id), out var pendingStep);
                            var state = pendingStep is null ? assessment.State : "Needs repair";
                            var action = !assessment.Required ? "Optional"
                                         : pendingStep is not null ||
                                                 assessment.ReasonCodes.Any(reason => reason is
                                                                            "RestartRequired" or
                                                                            "PrivilegeRequired")
                                             ? "Action required"
                                         : state == "Ready"                       ? "Up to date"
                                         : state == "Unsupported"                 ? "Unsupported"
                                         : !backend.HasRecipe(profile.Id, policy) ? "Unavailable"
                                         : state == "Missing"                     ? "Install"
                                         : state == "Needs repair"                ? "Repair"
                                                                   : "Check required";
                            var detail =
                                pendingStep is not null
                                    ? Explain(pendingStep.Reason.ToString())
                                    : string.Join(" ", assessment.ReasonCodes.Select(Explain));
                            if (action is "Install" or "Repair")
                            {
                                detail = (detail + " " + backend.DescribeAction(profile.Id, policy))
                                             .Trim();
                            }

                            return new SetupAction(profile.Id, assessment.Id, state, action,
                                                   detail);
                        }))
                .ToArray();
        }
    }

    public void Select(string id, bool value)
    {
        if (IsBusy || !Catalog.Profiles.Any(profile => profile.Id == id))
        {
            throw new InvalidOperationException("Profile selection is unavailable.");
        }

        if (value ? selected.Add(id) : selected.Remove(id))
        {
            health = null;
            progress = null;
            message = "Selection changed. Scan again to review available actions.";
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public Task ScanAsync()
    {
        return ExecuteAsync(prepare: false);
    }

    public Task PrepareAsync()
    {
        return ExecuteAsync(prepare: true);
    }

    public Task ReconcileAsync()
    {
        if (!CanReconcile)
        {
            throw new InvalidOperationException("No pending installer action can be rechecked.");
        }

        return ExecuteAsync(prepare: false, reconcile: true);
    }

    public void Cancel()
    {
        operation?.Cancel();
    }

    private async Task ExecuteAsync(bool prepare, bool reconcile = false)
    {
        if (prepare ? !CanPrepare : !CanScan)
        {
            throw new InvalidOperationException(
                "Scan the selected profiles before preparing available actions.");
        }

        var selection = Selected;
        var started = Stopwatch.GetTimestamp();
        var current = ++generation;
        using var cancellation = new CancellationTokenSource();
        operation = cancellation;
        progress = null;
        message =
            prepare ? "Preparing selected environments..." : "Checking selected environments...";
        Changed?.Invoke(this, EventArgs.Empty);
        var updates =
            new Progress<PreparationProgress>(value =>
                                              {
                                                  if (current == generation && IsBusy)
                                                  {
                                                      progress = value;
                                                      Changed?.Invoke(this, EventArgs.Empty);
                                                  }
                                              });
        try
        {
            if (prepare)
            {
                preparation = await Task.Run(
                    () => backend.PrepareAsync(selection, updates, cancellation.Token));

                cancellation.Token.ThrowIfCancellationRequested();
                foreach (var step in preparation.Steps.Where(
                             step => step.Reason is PreparationReason
                                         .RestartRequired or PreparationReason.PrivilegeRequired))
                {
                    pending[(step.ProfileId, step.RequirementId)] = step;
                }

                health = preparation.Health;
                message = preparation.Status switch {
                    PreparationStatus.Completed => "Selected environments are ready.",
                    PreparationStatus.Blocked =>
                        "Some actions need attention. Review the details below.",
                    _ => "Preparation failed for one or more components. Review the details " +
                         "before retrying."
                };
            }
            else
            {
                health = await Task.Run(
                    () => reconcile ? backend.ReconcileAsync(selection, cancellation.Token)
                                    : backend.ScanAsync(selection, cancellation.Token));

                cancellation.Token.ThrowIfCancellationRequested();
                if (reconcile)
                {
                    foreach (var key in pending.Keys
                                 .Where(key => health.ResolvedProfiles.Contains(key.Profile))
                                 .ToArray())
                    {
                        pending.Remove(key);
                    }
                }

                message = HasPendingAction ? "An installer action is still pending. Review the " +
                                                 "required restart or privileges."
                          : health.State == "Ready"
                              ? "Selected environments are ready. No installation is needed."
                              : "Scan complete. Review the available actions and checks below.";
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            health = null;
            message = "Operation cancelled. Scan again before continuing.";
        }
        catch (Exception error)
        {
            log?.Write(LogLevel.Error, "setup", () => "Setup operation failed.");

            health = null;
            message = error switch {
                InvalidDataException or JsonException =>
                    "The local repair record is unreadable. Preserve the repair directory for " +
                    "diagnosis; no installer was started from this record.",
                IOException => "The operation failed. Setup storage is unavailable or another " +
                               "setup instance is " +
                               "working. Close other instances, check access and scan again.",
                _ => "The operation failed. Scan again and review the environment checks."
            };
        }
        finally
        {
            log?.Write(LogLevel.Info, "setup", () => "Setup operation finished.",
                () => new Dictionary<string, object?> { ["operation"] = prepare ? "prepare" : "scan",
                    ["duration_us"] = Stopwatch.GetElapsedTime(started).Ticks / 10 });

            operation = null;
            generation++;
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public string ProfileState(string id)
    {
        if (pending.Keys.Any(key => key.Profile == id))
        {
            return "Needs repair";
        }

        return health?.Profiles.FirstOrDefault(profile => profile.Id == id)?.State ?? "Not checked";
    }

    public static string Explain(string reason)
    {
        return reason switch {
            "NotFound" => "Not found in the configured tool locations.",
            "VersionMismatch" => "The installed version does not match the required version.",
            "ArchitectureMismatch" => "A Windows x64 tool is required.",
            "FingerprintMismatch" or "StateStale" =>
                "Installed files or configuration have changed.",
            "RecipeUnavailable" =>
                "An approved tool definition is not available in this distribution.",
            "ProbeTimedOut" => "The check timed out. Try scanning again.",
            "ProbeFailed" or "MissingEvidence" =>
                "The check was inconclusive. Try scanning again.",
            "UnsupportedPlatform" or "UnsupportedArchitecture" =>
                "This environment requires Windows x64.",
            "DependencyNotReady" => "A prerequisite environment is not ready.",
            "RestartRequired" => "The installer requires a Windows restart before continuing.",
            "PrivilegeRequired" =>
                "The installer requires additional privileges. Review it before continuing.",
            "IncompleteSetup" => "Installation is incomplete.",
            "None" => "",
            "ExecutionFailed" => "The installer did not complete successfully.",
            "ValidationFailed" => "The component is still unhealthy after installation.",
            "AcquisitionFailed" =>
                "The required artifact could not be acquired. Check connectivity or supply the " +
                "approved offline artifact, then retry.",
            "ArtifactRejected" => "The artifact failed integrity verification.",
            _ => reason
        };
    }

    public void Dispose()
    {
        Cancel();
    }
}

/// <summary>Embedded approved policy; creating the application never executes a tool.</summary>
public sealed class WindowsSetupBackend : ISetupBackend
{
    private readonly RecipeRunner runner;
    private readonly RepairPlan repair;
    private readonly SetupRecipe[] recipes;
    private readonly string interpreterDescription;
    public string CatalogJson { get; }
    public bool SupportsRepair => true;

    public WindowsSetupBackend(string localRoot, TelemetrySession telemetry, Log? log = null)
    {
        using var sdk = JsonDocument.Parse(Resource("SdkPolicy"));
        using var python = JsonDocument.Parse(Resource("PythonPolicy"));
        var version = sdk.RootElement.GetProperty("sdk").GetProperty("version").GetString()!;
        var interpreter = python.RootElement.GetProperty("interpreter");
        var versions = interpreter.GetProperty("versions")
                           .EnumerateArray()
                           .Select(value => value.GetString()!)
                           .ToArray();
        CatalogJson = CreateCatalog(version, versions);
        var catalog = EnvironmentProbe.ReadCatalog(CatalogJson);
        var discovery = new ToolDiscovery();
        RegisterPaths("managed.sdk", ToolProtocol.DotnetSdk, "dotnet.exe",
                      Path.Combine(System.Environment.GetFolderPath(
                                       System.Environment.SpecialFolder.ProgramFiles),
                                   "dotnet", "dotnet.exe"));
        RegisterPaths("native.cmake", ToolProtocol.CMake, "cmake.exe");
        RegisterPaths("native.ninja", ToolProtocol.Ninja, "ninja.exe");
        var interpreterRoot = Path.Combine(localRoot, "python312");
        RegisterPaths("python.interpreter", ToolProtocol.Python, "python.exe",
                      Path.Combine(interpreterRoot, "python.exe"),
                      Path.Combine(System.Environment.GetFolderPath(
                                       System.Environment.SpecialFolder.LocalApplicationData),
                                   "Programs", "Python", "Python312", "python.exe"));
        var requirement = catalog.Profiles.Single(profile => profile.Id == "python-pipeline")
                              .Requirements.Single(item => item.Id == "python.interpreter");
        var bootstrap = interpreter.GetProperty("bootstrap");
        var arguments =
            bootstrap.GetProperty("arguments")
                .EnumerateArray()
                .Select(value => value.GetString()!.Replace("{interpreter_root}", interpreterRoot))
                .ToArray();
        var artifact = new SetupArtifact(
            "cpython-installer", new Uri(bootstrap.GetProperty("url").GetString()!),
            "python-" + bootstrap.GetProperty("version").GetString() + "-amd64.exe",
            bootstrap.GetProperty("sha256").GetString()!);
        interpreterDescription =
            $"Install official CPython {bootstrap.GetProperty("version").GetString()} for this user at {interpreterRoot}.";
        recipes = [new(requirement.RecipeRef!, "python-pipeline", requirement.Id,
                       SetupRecipe.Fingerprint(requirement), [artifact],
                       (context, token) =>
                           RecipeProcess.ExecuteAsync(context.Artifacts[artifact.Id].Path,
                                                      arguments, TimeSpan.FromMinutes(15), token))];

        repair = new(Path.Combine(localRoot, "repair"));
        runner = new(discovery, recipes, new ArtifactAcquisition(Path.Combine(localRoot, "cache")),
                     telemetry, log, repair);

        void RegisterPaths(string id, ToolProtocol protocol, string executable,
                           params string[] known)
        {
            var path =
                (System.Environment.GetEnvironmentVariable("PATH") ?? "")
                    .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                    .Where(directory => Path.IsPathFullyQualified(directory) &&
                                        !directory.Contains("WindowsApps",
                                                            StringComparison.OrdinalIgnoreCase))
                    .Select(directory => Path.Combine(directory, executable))
                    .Where(File.Exists);
            var candidates =
                known.Concat(path).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            if (candidates.Length > 0)
            {
                discovery.RegisterExecutable(id, protocol, candidates);
            }
        }
    }

    public string DescribeAction(string profileId, ToolRequirement requirement)
    {
        var previous = repair.LastObserved.SingleOrDefault(
            entry => entry.ProfileId == profileId && entry.RequirementId == requirement.Id);
        var detail = previous is null
                         ? ""
                         : $"Previous preparation stopped at {previous.Stage}. " +
                               (previous.Reason == PreparationReason.None
                                    ? "Recheck before retrying."
                                    : ProfileViewModel.Explain(previous.Reason.ToString()));
        return HasRecipe(profileId, requirement) ? (interpreterDescription + " " + detail).Trim()
                                                 : detail;
    }

    public bool HasRecipe(string profileId, ToolRequirement requirement)
    {
        return recipes.Any(recipe => recipe.ProfileId == profileId &&
                                     recipe.RequirementId == requirement.Id &&
                                     recipe.Id == requirement.RecipeRef &&
                                     recipe.PolicySha256 == SetupRecipe.Fingerprint(requirement));
    }

    public Task<EnvironmentHealth> ScanAsync(string[] selected, CancellationToken token)
    {
        return runner.ScanAsync(CatalogJson, selected, cancellationToken: token);
    }

    public Task<EnvironmentHealth> ReconcileAsync(string[] selected, CancellationToken token)
    {
        return runner.ScanAsync(CatalogJson, selected, cancellationToken: token,
                                acknowledgePendingActions: true);
    }

    public Task<EnvironmentState> PrepareAsync(string[] selected,
                                               IProgress<PreparationProgress> progress,
                                               CancellationToken token)
    {
        return runner.RunAsync(CatalogJson, selected, progress, cancellationToken: token);
    }

    private static string Resource(string name)
    {
        using var stream =
            Assembly.GetExecutingAssembly().GetManifestResourceStream("Raiders.Setup." + name)!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    private static string CreateCatalog(string sdkVersion, string[] pythonVersions)
    {
        var native = new[] { Requirement("native.msvc", "native-toolchain",
                                         new("exact", "19.44.35229.0"), "native-toolchain-v1"),
                             Requirement("native.toolset", "native-toolchain",
                                         new("exact", "14.44.35207"), "native-toolchain-v1"),
                             Requirement("native.windows-sdk", "native-sdk",
                                         new("exact", "10.0.26100.0"), "native-toolchain-v1"),
                             Requirement("native.cmake", "build-tool", new("exact", "3.31.6-msvc6"),
                                         "native-build-tools-v1"),
                             Requirement("native.ninja", "build-tool", new("exact", "1.12.1"),
                                         "native-build-tools-v1") };
        EnvironmentProfile[] profiles = [
            new("native-development", "Native development", ["native-developer"],
                new("windows", "x64"), [], native),
            new("managed-tooling", ".NET 10 tooling", ["tooling-developer"], new("windows", "x64"),
                [],
                [
                    Requirement("managed.sdk", "managed-sdk",
                                new("sdk-feature-band", sdkVersion, RollForward: "latestPatch",
                                    AllowPrerelease: false),
                                "dotnet-sdk-v1"),
                    Requirement("managed.packages", "package-feed",
                                new("configuration", Reference: "nuget-config-v1"),
                                "nuget-restore-v1", ["managed.sdk"], architecture: "any")
                ]),
            new("python-pipeline", "Python pipeline", ["pipeline-developer"], new("windows", "x64"),
                [],
                [
                    Requirement("python.interpreter", "python-interpreter",
                                new("one-of", Values: pythonVersions), "python-interpreter-v1"),
                    Requirement("python.build", "python-environment",
                                new("configuration", Reference: "python-build-v1"),
                                "python-build-v1", ["python.interpreter"]),
                    Requirement("python.tools", "python-environment",
                                new("configuration", Reference: "python-tools-v1"),
                                "python-tools-v1", ["python.interpreter", "python.build"])
                ]),
            new("lua-development", "Lua development", ["lua-developer"], new("windows", "x64"), [],
                [
                    Requirement("lua.runtime", "lua-toolchain",
                                new("manifest", Reference: "luajit-development-v1"),
                                "luajit-development-v1"),
                    Requirement("lua.editor", "code-editor",
                                new("manifest", Reference: "vscode-lua-v1"), "vscode-lua-v1")
                ]),
            new("designer-tools", "Designer tools", ["designer"], new("windows", "x64"), [],
                [
                    Requirement("designer.sdk", "artifact-bundle",
                                new("manifest", Reference: "designer-sdk-v1"), "designer-sdk-v1"),
                    new("designer.lua-editor", "code-editor", false, "lua.editor", "x64",
                        new("manifest", Reference: "vscode-lua-v1"), [], "vscode-lua-v1")
                ])
        ];
        var catalog = new ProfileCatalog(
            1, "raiders.setup.windows", 1,
            new("csharp", "net10.0-windows", "win-x64", "self-contained", false, false, false),
            profiles);
        return JsonSerializer.Serialize(catalog, new JsonSerializerOptions {
            PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
        });
    }

    private static ToolRequirement Requirement(string id, string kind, ToolConstraint constraint,
                                               string recipe, string[]? dependencies = null,
                                               string architecture = "x64")
    {
        return new(id, kind, true, id, architecture, constraint, dependencies ?? [], recipe);
    }
}
