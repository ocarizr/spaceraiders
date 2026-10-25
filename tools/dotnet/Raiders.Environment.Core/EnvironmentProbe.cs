using System.Runtime.InteropServices;
using System.Diagnostics.CodeAnalysis;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using Raiders.Diagnostics;

namespace Raiders.Environment.Core;

public sealed record ToolConstraint(string Kind, string? Value = null, string[]? Values = null,
                                    string? Reference = null, string? RollForward = null,
                                    bool? AllowPrerelease = null);
public sealed record ToolRequirement(string Id, string Kind, bool Required, string ProbeId,
                                     string Architecture, ToolConstraint Constraint,
                                     string[] DependsOn, string? RecipeRef);
public sealed record ProfileTarget(string OperatingSystem, string Architecture);
public sealed record SetupHost(string Language, string Framework, string RuntimeIdentifier,
                               string Deployment, bool RequiresInstalledSdk, bool RequiresPython,
                               bool RequiresGame);
public sealed record EnvironmentProfile(string Id, string Title, string[] Roles,
                                        ProfileTarget Target, string[] DependsOn,
                                        ToolRequirement[] Requirements);
public sealed record ProfileCatalog(int SchemaVersion, string CatalogId, int Revision,
                                    SetupHost SetupHost, EnvironmentProfile[] Profiles);
public sealed record MachineIdentity(string OperatingSystem, string Architecture);
public sealed record EvidenceLocation(string Kind, string? Value);
public sealed record ProbeEvidence(string ProbeId, string Outcome, bool Complete, string ObservedAt,
                                   string? ObservedVersion, string ObservedArchitecture,
                                   string? ConfigurationSha256, EvidenceLocation Location);
public sealed record RequirementHealth(string Id, bool Required, string State, string[] ReasonCodes,
                                       ProbeEvidence[] Evidence);
public sealed record ProfileDependency(string ProfileId, string State);
public sealed record ProfileHealth(string Id, string State, string[] ReasonCodes,
                                   ProfileDependency[] Dependencies,
                                   RequirementHealth[] Requirements);

public sealed record EnvironmentHealth(int SchemaVersion, string SessionId, string ScanId,
                                       string CatalogId, string CatalogSha256, string Visibility,
                                       MachineIdentity Machine, string StartedAt,
                                       string CompletedAt, bool ScanComplete,
                                       string[] SelectedProfiles, string[] ResolvedProfiles,
                                       string State, string[] ReasonCodes, ProfileHealth[] Profiles)
{
    public string ToJson()
    {
        return JsonSerializer.Serialize(this, EnvironmentProbe.JsonOptions);
    }
}

/// <summary>
/// Terminal, read-only scans of selected Y01 profiles. Discovery is injectable for
/// deterministic tests; no saved Ready result is accepted as current evidence.
/// </summary>
/// <remarks>
/// Register trusted tool paths/manifests in ToolDiscovery, then call ScanAsync with
/// the v1 catalog JSON and selected profile IDs. Only the prerequisite closure is
/// scanned. Unregistered policy is Unknown; acquiring tools is a separate operation.
/// ScanAsync returns a completed report or propagates caller cancellation, rather
/// than exposing a mutable partial report. ToJson uses the Y01 wire names and UTC.
/// Reports redact local paths by default. Local visibility requires an explicit
/// shareable: false argument. The caller owns the telemetry session and log sink;
/// probe elapsed time is logged, and synchronous assessment scopes follow the
/// RaidersEnableProfiling build switch. No diagnostic writes to stdout.
/// </remarks>
public sealed class EnvironmentProbe(IToolDiscovery discovery, TelemetrySession telemetry,
                                     Log? log = null, TimeProvider? clock = null)
{
    internal static readonly JsonSerializerOptions JsonOptions =
        new() { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
                UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
                RespectRequiredConstructorParameters = true, RespectNullableAnnotations = true,
                WriteIndented = true };
    private static readonly Regex Id = new("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$");
    internal static readonly Regex VersionText =
        new("^[0-9]+(?:\\.[0-9]+){1,3}(?:[-+][A-Za-z0-9.-]+)?$");
    internal static readonly Regex Sha256 = new("^[a-f0-9]{64}$");
    private static readonly string[] Priority =
        ["Unsupported", "Needs repair", "Missing", "Checking", "Unknown", "Ready"];
    private static readonly HashSet<string> Kinds = [
        "native-toolchain", "native-sdk", "build-tool", "managed-sdk", "package-feed",
        "python-interpreter", "python-environment", "lua-toolchain", "code-editor",
        "artifact-bundle"
    ];
    private static readonly HashSet<string> Roles = [
        "native-developer", "tooling-developer", "pipeline-developer", "lua-developer", "designer"
    ];

    public static string CatalogFingerprint(string json)
    {
        var normalized = json.TrimStart('\uFEFF').Replace("\r\n", "\n").Replace('\r', '\n');
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(normalized)));
    }

    public static ProfileCatalog ReadCatalog(string json)
    {
        using var document = JsonDocument.Parse(json.TrimStart('\uFEFF'));
        ValidateMembers(document.RootElement);
        var catalog =
            JsonSerializer.Deserialize<ProfileCatalog>(json.TrimStart('\uFEFF'), JsonOptions) ??
            throw new ArgumentException("Catalog is null.");
        Require(catalog.SchemaVersion == 1 && catalog.Revision > 0 && ValidId(catalog.CatalogId));
        Require(catalog.SetupHost == new SetupHost("csharp", "net10.0-windows", "win-x64",
                                                   "self-contained", false, false, false));
        Require(catalog.Profiles is { Length : > 0 });
        var profiles = Unique(catalog.Profiles, profile => profile.Id);
        foreach (var profile in catalog.Profiles)
        {
            Require(!string.IsNullOrWhiteSpace(profile.Title) && profile.Title.Length <= 128);
            Require(profile.Target == new ProfileTarget("windows", "x64"));
            Require(profile.Roles is { Length : > 0 } &&
                    profile.Roles.Distinct().Count() == profile.Roles.Length &&
                    profile.Roles.All(Roles.Contains));
            Require(profile.Requirements is { Length : > 0 } &&
                    profile.Requirements.Any(item => item.Required));

            var requirements = Unique(profile.Requirements, item => item.Id);
            foreach (var requirement in profile.Requirements)
            {
                Require(Kinds.Contains(requirement.Kind) && ValidId(requirement.ProbeId));
                Require(requirement.Architecture is "x64" or "any");
                Require(requirement.RecipeRef is null || ValidId(requirement.RecipeRef));
                ValidateConstraint(requirement.Constraint);
                Require(requirement.DependsOn is not null);
                foreach (var dependency in requirement.DependsOn)
                {
                    Require(requirements.TryGetValue(dependency, out var prerequisite));
                    Require(!requirement.Required || prerequisite!.Required);
                }
            }

            Order(requirements.Keys, id => requirements[id].DependsOn);
        }

        Order(profiles.Keys, id => profiles[id].DependsOn);

        foreach (var designer in catalog.Profiles.Where(profile =>
                                                            profile.Roles.Contains("designer")))
        {
            var closure = Order([designer.Id], id => profiles[id].DependsOn);
            Require(closure.SelectMany(id => profiles[id].Requirements)
                        .Where(item => item.Required)
                        .All(item => item.Kind is "artifact-bundle" or "code-editor" or
                                                  "lua-toolchain"));
        }

        return catalog;
    }

    public async Task<EnvironmentHealth> ScanAsync(string catalogJson,
                                                   IEnumerable<string> selection,
                                                   bool shareable = true,
                                                   MachineIdentity? machine = null,
                                                   CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(discovery);
        ArgumentNullException.ThrowIfNull(telemetry);
        var catalog = ReadCatalog(catalogJson);
        var selected = selection.ToArray();
        Require(selected.Length > 0 && selected.Distinct().Count() == selected.Length);
        var profiles = catalog.Profiles.ToDictionary(profile => profile.Id);
        Require(selected.All(profiles.ContainsKey));
        var resolved = Order(selected, id => profiles[id].DependsOn);
        machine ??=
            new MachineIdentity(OperatingSystem.IsWindows() ? "windows" : "other",
                                RuntimeInformation.OSArchitecture.ToString().ToLowerInvariant());
        Require(machine.OperatingSystem is "windows" or "other");
        Require(machine.Architecture is "x64" or "x86" or "arm64" or "unknown");
        var started = Now();
        var scanId = Guid.NewGuid().ToString("N");
        using var context = telemetry.PushContext(correlationId: scanId);
        var results = new Dictionary<string, ProfileHealth>();
        var hostReason = machine.OperatingSystem != "windows"           ? "UnsupportedPlatform"
                         : machine.Architecture is "x86" or "arm64" ? "UnsupportedArchitecture"
                                                                        : null;
        foreach (var id in resolved)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var profile = profiles[id];
            var requirements = new List<RequirementHealth>();
            foreach (var requirement in profile.Requirements)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var probeStarted = Stopwatch.GetTimestamp();
                RequirementHealth health;
                if (hostReason is not null)
                {
                    health = Result(requirement, "Unsupported", hostReason, "unsupported", true,
                                    null, shareable);
                }
                else if (machine.Architecture == "unknown")
                {
                    health = Result(requirement, "Unknown", "MissingEvidence", "not-run", false,
                                    null, shareable);
                }
                else
                {
                    DiscoveryResult observation;
                    try
                    {
                        observation = await discovery.DiscoverAsync(requirement, cancellationToken);
                        cancellationToken.ThrowIfCancellationRequested();
                    }
                    catch (OperationCanceledException)
                        when (cancellationToken.IsCancellationRequested)
                    {
                        throw;
                    }
                    catch (TimeoutException)
                    {
                        observation = new("timed-out", []);
                    }
                    catch (Exception)
                    {
                        observation = new("failed", []);
                    }

                    var scope = new ProfileScope();
                    ProfileScope.Begin(ref scope, telemetry, "environment", "assessment");
                    using (scope)
                    {
                        health = Assess(requirement, observation, shareable);
                    }
                }

                requirements.Add(health);
                log?.Write(LogLevel.Info, "environment", () => "Requirement probe completed.",
                    () => new Dictionary<string, object?> { ["probe_id"] = requirement.ProbeId, ["state"] = health.State,
                        ["duration_us"] = Stopwatch.GetElapsedTime(probeStarted).Ticks / 10 });
            }

            var dependencies =
                profile.DependsOn.Select(dep => new ProfileDependency(dep, results[dep].State))
                    .ToArray();
            var required = requirements.Where(item => item.Required).ToArray();
            var state = Aggregate(required.Select(item => item.State)
                                      .Concat(dependencies.Select(item => item.State)));
            var reasons = required.SelectMany(item => item.ReasonCodes).ToList();
            if (dependencies.Any(dep => dep.State != "Ready"))
            {
                reasons.Add("DependencyNotReady");
            }

            results[id] =
                new(id, state, Reasons(state, reasons), dependencies, requirements.ToArray());
        }

        var all = resolved.Select(id => results[id]).ToArray();
        var aggregate = Aggregate(all.Select(profile => profile.State));
        return new(1, telemetry.SessionId, scanId, catalog.CatalogId,
                   CatalogFingerprint(catalogJson), shareable ? "shareable" : "local", machine,
                   started, Now(), true, selected, resolved, aggregate,
                   Reasons(aggregate, all.SelectMany(profile => profile.ReasonCodes)), all);
    }

    private RequirementHealth Assess(ToolRequirement requirement, DiscoveryResult? result,
                                     bool shareable)
    {
        if (result is null ||
            result.Status is not("found" or "absent" or "failed" or "timed-out" or
                                 "unresolved") ||
            result.Candidates is null)
        {
            return Result(requirement, "Unknown", "ProbeFailed", "failed", false, null, shareable);
        }

        if (result.Status != "found")
        {
            var (state, reason, outcome, complete) =
                result.Status switch { "absent" => ("Missing", "NotFound", "absent", true),
                                       "timed-out" => ("Unknown", "ProbeTimedOut", "failed", false),
                                       "unresolved" =>
                                           ("Unknown", "RecipeUnavailable", "not-run", false),
                                       _ => ("Unknown", "ProbeFailed", "failed", false) };
            return Result(requirement, state, reason, outcome, complete, null, shareable);
        }

        if (result.Candidates.Length == 0 ||
            result.Candidates.Any(
                candidate =>
                    candidate is null ||
                    candidate.Architecture is not("x64" or "x86" or "arm64" or "unknown" or
                                                  "any") ||
                    candidate.Version is not null && (candidate.Version.Length > 128 ||
                                                      !VersionText.IsMatch(candidate.Version)) ||
                    candidate.Fingerprint is not null && !Sha256.IsMatch(candidate.Fingerprint)))
        {
            return Result(requirement, "Unknown", "MissingEvidence", "failed", false, null,
                          shareable);
        }

        var assessed =
            result.Candidates
                .Select(candidate => AssessCandidate(requirement, candidate,
                                                     result.ExpectedFingerprint, shareable))
                .ToArray();
        // A compatible installation wins even if another SDK/interpreter is old.
        return assessed.FirstOrDefault(item => item.State == "Ready") ??
               assessed.FirstOrDefault(item => item.State == "Needs repair") ?? assessed[0];
    }

    private RequirementHealth AssessCandidate(ToolRequirement requirement, ToolCandidate candidate,
                                              string? expectedFingerprint, bool shareable)
    {
        var constraint = requirement.Constraint;
        string? reason = null;
        if (constraint.Kind is "configuration" or "manifest" &&
            (expectedFingerprint is null || !Sha256.IsMatch(expectedFingerprint)))
        {
            return Result(requirement, "Unknown", "RecipeUnavailable", "not-run", false, null,
                          shareable);
        }

        if (candidate.Problem is "IncompleteSetup" or "StateStale")
        {
            return Result(requirement, "Needs repair", candidate.Problem, "mismatch", true,
                          candidate, shareable);
        }

        if (candidate.Problem is not null)
        {
            return Result(requirement, "Unknown", "ProbeFailed", "failed", false, null, shareable);
        }

        if (candidate.Architecture == "unknown" ||
            requirement.Architecture == "x64" && candidate.Architecture == "any")
        {
            return Result(requirement, "Unknown", "MissingEvidence", "failed", false, null,
                          shareable);
        }

        if (requirement.Architecture != "any" && candidate.Architecture != requirement.Architecture)
        {
            reason = "ArchitectureMismatch";
        }
        else if (constraint.Kind is "configuration" or "manifest")
        {
            if (candidate.Fingerprint is null)
            {
                return Result(requirement, "Unknown", "MissingEvidence", "failed", false, null,
                              shareable);
            }

            if (candidate.Fingerprint != expectedFingerprint)
            {
                reason = "FingerprintMismatch";
            }
        }
        else if (candidate.Version is null)
        {
            return Result(requirement, "Unknown", "MissingEvidence", "failed", false, null,
                          shareable);
        }
        else if (!Matches(constraint, candidate.Version))
        {
            reason = "VersionMismatch";
        }

        return Result(requirement, reason is null ? "Ready" : "Needs repair", reason,
                      reason is null ? "satisfied" : "mismatch", true, candidate, shareable);
    }

    private RequirementHealth Result(ToolRequirement requirement, string state, string? reason,
                                     string outcome, bool complete, ToolCandidate? candidate,
                                     bool shareable)
    {
        var location = !shareable && candidate?.Location is { Length : > 0 and <= 1024 }
                           ? new EvidenceLocation("local-path", candidate.Location)
                           : new EvidenceLocation("redacted", null);
        return new(requirement.Id, requirement.Required, state, reason is null ? [] : [reason],
                   [new(requirement.ProbeId, outcome, complete, Now(), candidate?.Version,
                        candidate?.Architecture ?? "unknown", candidate?.Fingerprint, location)]);
    }

    private string Now()
    {
        return (clock ?? TimeProvider.System)
            .GetUtcNow()
            .UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'",
                                  System.Globalization.CultureInfo.InvariantCulture);
    }

    private static string Aggregate(IEnumerable<string> states)
    {
        var values = states.ToHashSet();
        return Priority.First(values.Contains);
    }

    private static string[] Reasons(string state, IEnumerable<string> reasons)
    {
        return state == "Ready" ? [] : reasons.Distinct().Order().ToArray();
    }

    private static bool Matches(ToolConstraint constraint, string observed)
    {
        if (constraint.Kind == "exact")
        {
            return constraint.Value == observed;
        }

        if (constraint.Kind == "one-of")
        {
            return constraint.Values!.Contains(observed);
        }

        if (!Version.TryParse(observed, out var version) ||
            !Version.TryParse(constraint.Value, out var baseline))
        {
            return false;
        }

        return version.Major == baseline.Major && version.Minor == baseline.Minor &&
               version.Revision == -1 && version.Build / 100 == baseline.Build / 100 &&
               version.Build >= baseline.Build;
    }

    private static void ValidateConstraint(ToolConstraint constraint)
    {
        Require(constraint is not null);
        switch (constraint.Kind)
        {
        case "exact":
        case "sdk-feature-band":
            Require(constraint.Value is { Length : <= 128 } &&
                    VersionText.IsMatch(constraint.Value));
            Require(constraint.Values is null && constraint.Reference is null);
            if (constraint.Kind == "sdk-feature-band")
            {
                Require(Regex.IsMatch(constraint.Value!, "^10\\.[0-9]+\\.[0-9]{3}$") &&
                        constraint.RollForward == "latestPatch" &&
                        constraint.AllowPrerelease == false);
            }
            else
            {
                Require(constraint.RollForward is null && constraint.AllowPrerelease is null);
            }

            break;
        case "one-of":
            Require(constraint.Values is { Length : > 0 } &&
                    constraint.Values.All(value => value is { Length : <= 128 } &&
                                                   VersionText.IsMatch(value)) &&
                    constraint.Values.Distinct().Count() == constraint.Values.Length);

            Require(constraint.Value is null && constraint.Reference is null &&
                    constraint.RollForward is null && constraint.AllowPrerelease is null);
            break;
        case "configuration":
        case "manifest":
            Require(ValidId(constraint.Reference) && constraint.Value is null &&
                    constraint.Values is null && constraint.RollForward is null &&
                    constraint.AllowPrerelease is null);
            break;
        default:
            throw new ArgumentException("Unknown constraint kind.");
        }
    }

    private static Dictionary<string, T> Unique<T>(T[] items, Func<T, string> key)
    {
        Require(items.All(item => item is not null && ValidId(key(item))));

        Require(items.Select(key).Distinct().Count() == items.Length);
        return items.ToDictionary(key);
    }

    private static string[] Order(IEnumerable<string> roots, Func<string, string[]> dependencies)
    {
        var visited = new HashSet<string>();
        var active = new HashSet<string>();
        var ordered = new List<string>();
        void Visit(string id)
        {
            Require(ValidId(id) && !active.Contains(id));
            if (visited.Contains(id))
            {
                return;
            }

            active.Add(id);
            string[] next;
            try
            {
                next = dependencies(id);
            }
            catch (KeyNotFoundException)
            {
                throw new ArgumentException("Unknown prerequisite.");
            }

            Require(next is not null && next.Distinct().Count() == next.Length);
            foreach (var dependency in next)
            {
                Visit(dependency);
            }

            active.Remove(id);
            visited.Add(id);
            ordered.Add(id);
        }

        foreach (var root in roots)
        {
            Visit(root);
        }

        return ordered.ToArray();
    }

    private static bool ValidId(string? value)
    {
        return value is not null && Id.IsMatch(value);
    }

    private static void ValidateMembers(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var members = new HashSet<string>(StringComparer.Ordinal);
            foreach (var member in element.EnumerateObject())
            {
                Require(members.Add(member.Name));
                ValidateMembers(member.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in element.EnumerateArray())
            {
                ValidateMembers(item);
            }
        }
    }

    private static void Require([DoesNotReturnIf(false)] bool condition)
    {
        if (!condition)
        {
            throw new ArgumentException("Invalid environment profile catalog or selection.");
        }
    }
}
