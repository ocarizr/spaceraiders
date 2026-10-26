using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Raiders.Diagnostics;

namespace Raiders.Environment.Core;

public sealed record RecipeContext(string RunId, string ProfileId, ToolRequirement Requirement,
                                   IReadOnlyDictionary<string, VerifiedArtifact> Artifacts);

/// <summary>
/// Trusted host registration, bound to the complete requirement policy. Catalogs
/// select registry IDs; they cannot supply executable paths, arguments or URLs.
/// Actions can call RecipeProcess for native installers or the Q06/Q07 Python CLIs.
/// </summary>
public sealed record
SetupRecipe(string Id, string ProfileId, string RequirementId, string PolicySha256,
            IReadOnlyList<SetupArtifact> Artifacts,
            Func<RecipeContext, CancellationToken, Task<RecipeExecutionStatus>> ExecuteAsync)
{
    public static string Fingerprint(ToolRequirement requirement)
    {
        var json = JsonSerializer.Serialize(requirement, EnvironmentProbe.JsonOptions);
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(json)));
    }
}

/// <summary>
/// Sequential prerequisite execution for one selected closure. Installers run only
/// for Missing/Needs repair observations; Unknown never authorizes a reinstall.
/// Independent steps continue after a failure; dependent steps remain blocked.
/// </summary>
public sealed class RecipeRunner
{
    private readonly EnvironmentProbe probe;
    private readonly ArtifactAcquisition acquisition;
    private readonly TelemetrySession telemetry;
    private readonly Log? log;
    private readonly RepairPlan? repair;
    private readonly Dictionary<(string Profile, string Requirement), SetupRecipe> recipes = new();
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly Dictionary<(string Profile, string Requirement), PreparationReason> pending =
        new();

    public RecipeRunner(IToolDiscovery discovery, IEnumerable<SetupRecipe> recipes,
                        ArtifactAcquisition acquisition, TelemetrySession telemetry,
                        Log? log = null, RepairPlan? repair = null)
    {
        ArgumentNullException.ThrowIfNull(acquisition);
        ArgumentNullException.ThrowIfNull(telemetry);
        probe = new(discovery, telemetry, log);
        this.acquisition = acquisition;
        this.telemetry = telemetry;
        this.log = log;
        this.repair = repair;
        foreach (var recipe in recipes)
        {
            ArgumentNullException.ThrowIfNull(recipe);
            if (!EnvironmentProbe.Sha256.IsMatch(recipe.PolicySha256) ||
                recipe.ExecuteAsync is null || recipe.Artifacts is null ||
                recipe.Artifacts.Select(item => item.Id).Distinct().Count() !=
                    recipe.Artifacts.Count)
            {
                throw new ArgumentException("Invalid approved recipe registration.");
            }

            foreach (var artifact in recipe.Artifacts)
            {
                ArtifactAcquisition.Validate(artifact);
            }

            this.recipes.Add((recipe.ProfileId, recipe.RequirementId),
                             recipe with { Artifacts = recipe.Artifacts.ToArray() });
        }
    }

    public async Task<EnvironmentHealth> ScanAsync(string catalogJson,
                                                   IEnumerable<string> selection,
                                                   MachineIdentity? machine = null,
                                                   CancellationToken cancellationToken = default,
                                                   bool acknowledgePendingActions = false)
    {
        await gate.WaitAsync(cancellationToken);
        try
        {
            using var session = repair?.Open();
            var report = await probe.ScanAsync(catalogJson, selection, machine: machine,
                                               cancellationToken: cancellationToken);
            if (acknowledgePendingActions && session is not null)
            {
                foreach (var entry in session.Entries.ToArray())
                {
                    if (entry.CatalogId == report.CatalogId && IsPending(entry.Reason) &&
                        report.Profiles.Any(
                            profile =>
                                profile.Id == entry.ProfileId &&
                                profile.Requirements.Any(
                                    requirement => requirement.Id == entry.RequirementId &&
                                                   requirement.State is "Ready" or "Missing" or
                                                                        "Needs repair")))
                    {
                        session.Remove(entry.CatalogId, entry.ProfileId, entry.RequirementId);
                    }
                }
            }

            return ApplyRepair(report, session);
        }
        finally
        {
            gate.Release();
        }
    }

    private static bool IsPending(PreparationReason reason)
    {
        return reason is PreparationReason.RestartRequired or PreparationReason.PrivilegeRequired;
    }

    private EnvironmentHealth ApplyRepair(EnvironmentHealth report, RepairPlan.Session? session,
                                          bool includeInterrupted = true)
    {
        if (session is null)
        {
            return EnvironmentState.ApplyActions(report, pending);
        }

        pending.Clear();
        var actions = new Dictionary<(string Profile, string Requirement), PreparationReason>();
        foreach (var entry in session.Entries.Where(entry => entry.CatalogId == report.CatalogId))
        {
            if (IsPending(entry.Reason))
            {
                pending[(entry.ProfileId, entry.RequirementId)] = entry.Reason;
                actions[(entry.ProfileId, entry.RequirementId)] = entry.Reason;
            }
            else if (includeInterrupted)
            {
                actions[(entry.ProfileId, entry.RequirementId)] = PreparationReason.IncompleteSetup;
            }
        }

        return EnvironmentState.ApplyActions(report, actions);
    }

    public async Task<EnvironmentState> RunAsync(string catalogJson, IEnumerable<string> selection,
                                                 IProgress<PreparationProgress>? progress = null,
                                                 MachineIdentity? machine = null,
                                                 CancellationToken cancellationToken = default)
    {
        await gate.WaitAsync(cancellationToken);
        try
        {
            return await RunSelectedAsync(catalogJson, selection.ToArray(), progress, machine,
                                          cancellationToken);
        }
        finally
        {
            gate.Release();
        }
    }

    private async Task<EnvironmentState> RunSelectedAsync(string catalogJson, string[] selected,
                                                          IProgress<PreparationProgress>? progress,
                                                          MachineIdentity? machine,
                                                          CancellationToken token)
    {
        var runId = Guid.NewGuid().ToString("N");
        using var context = telemetry.PushContext(correlationId: runId);
        var catalog = EnvironmentProbe.ReadCatalog(catalogJson);
        using var repairSession = repair?.Open();
        var profiles = catalog.Profiles.ToDictionary(profile => profile.Id);
        var steps = new List<RecipeStepResult>();
        var health = await CheckAsync();
        var planning = new ProfileScope();
        ProfileScope.Begin(ref planning, telemetry, "environment", "recipe-plan");
        (string Profile, ToolRequirement Requirement)[] plan;
        using (planning)
        {
            plan =
                health.ResolvedProfiles
                    .SelectMany(id => Order(profiles[id]).Select(requirement => (id, requirement)))
                    .ToArray();
        }

        try
        {
            foreach (var (profileId, requirement) in plan)
            {
                token.ThrowIfCancellationRequested();
                Report(profileId, requirement.Id, PreparationStage.Checking);
                health = await CheckAsync(profileId, requirement.Id);
                var profile = health.Profiles.Single(item => item.Id == profileId);
                var assessment = profile.Requirements.Single(item => item.Id == requirement.Id);
                if (assessment.State == "Ready")
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.AlreadyReady,
                           PreparationReason.None);
                    continue;
                }

                if (assessment.State == "Unsupported")
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked,
                           PreparationReason.UnsupportedHost);
                    continue;
                }

                if (pending.TryGetValue((profileId, requirement.Id), out var pendingAction))
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked, pendingAction);
                    continue;
                }

                if (profile.Dependencies.Any(item => item.State != "Ready") ||
                    requirement.DependsOn.Any(
                        id => profile.Requirements.Single(item => item.Id == id).State != "Ready"))
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked,
                           PreparationReason.DependencyNotReady);
                    continue;
                }

                if (assessment.State is not("Missing" or "Needs repair"))
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked,
                           PreparationReason.ProbeInconclusive);
                    continue;
                }

                if (!recipes.TryGetValue((profileId, requirement.Id), out var recipe) ||
                    recipe.Id != requirement.RecipeRef)
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked,
                           PreparationReason.RecipeUnavailable);
                    continue;
                }

                if (recipe.PolicySha256 != SetupRecipe.Fingerprint(requirement))
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked,
                           PreparationReason.PolicyChanged);
                    continue;
                }

                var artifacts = new Dictionary<string, VerifiedArtifact>();
                Record(profileId, requirement, PreparationStage.Acquiring);
                try
                {
                    foreach (var artifact in recipe.Artifacts)
                    {
                        Report(profileId, requirement.Id, PreparationStage.Acquiring, artifact.Id);
                        artifacts.Add(artifact.Id, await acquisition.AcquireAsync(
                                                       artifact, token, repairCorruptCache: true));
                    }
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception error)
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Failed,
                           error is ArtifactIntegrityException
                               ? PreparationReason.ArtifactRejected
                               : PreparationReason.AcquisitionFailed);
                    continue;
                }

                token.ThrowIfCancellationRequested();
                Record(profileId, requirement, PreparationStage.Executing);
                Report(profileId, requirement.Id, PreparationStage.Executing);
                var started = Stopwatch.GetTimestamp();
                RecipeExecutionStatus outcome;
                try
                {
                    outcome = await recipe.ExecuteAsync(
                        new(runId, profileId, requirement,
                            new ReadOnlyDictionary<string, VerifiedArtifact>(artifacts)),
                        token);
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception)
                {
                    outcome = RecipeExecutionStatus.Failed;
                }

                log?.Write(LogLevel.Info, "environment", () => "Recipe execution finished.",
                    () => new Dictionary<string, object?> { ["profile_id"] = profileId, ["requirement_id"] = requirement.Id,
                        ["outcome"] = outcome.ToString(), ["duration_us"] = Stopwatch.GetElapsedTime(started).Ticks / 10 });

                if (outcome is RecipeExecutionStatus.RestartRequired or
                        RecipeExecutionStatus.PrivilegeRequired)
                {
                    pending[(profileId, requirement.Id)] =
                        outcome == RecipeExecutionStatus.RestartRequired
                            ? PreparationReason.RestartRequired
                            : PreparationReason.PrivilegeRequired;
                    Record(profileId, requirement, PreparationStage.Validating,
                           pending[(profileId, requirement.Id)]);
                }
                else
                {
                    Record(profileId, requirement, PreparationStage.Validating);
                }

                token.ThrowIfCancellationRequested();
                Report(profileId, requirement.Id, PreparationStage.Validating);
                health = await CheckAsync(includeInterrupted: false);
                if (pending.TryGetValue((profileId, requirement.Id), out var action))
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Blocked, action);
                }
                else if (outcome != RecipeExecutionStatus.Completed)
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Failed,
                           PreparationReason.ExecutionFailed);
                }
                else if (health.Profiles.Single(item => item.Id == profileId)
                             .Requirements.Single(item => item.Id == requirement.Id)
                             .State != "Ready")
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Failed,
                           PreparationReason.ValidationFailed);
                }
                else
                {
                    Finish(profileId, requirement.Id, RecipeStepStatus.Completed,
                           PreparationReason.None);
                }
            }

            health = await CheckAsync();
            Report(null, null, PreparationStage.Finished);
            var status = steps.Any(step => step.Status == RecipeStepStatus.Failed)
                             ? PreparationStatus.Failed
                         : health.State != "Ready" ||
                                 steps.Any(step => step.Status == RecipeStepStatus.Blocked)
                             ? PreparationStatus.Blocked
                             : PreparationStatus.Completed;
            return new(runId, status, health, steps.ToArray());
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            Report(null, null, PreparationStage.Cancelled);
            throw;
        }

        async Task<EnvironmentHealth> CheckAsync(string? reconcileProfile = null,
                                                 string? reconcileRequirement = null,
                                                 bool includeInterrupted = true)
        {
            var report = await probe.ScanAsync(catalogJson, selected, machine: machine,
                                               cancellationToken: token);
            if (repairSession is not null && reconcileProfile is not null &&
                report.Profiles.Any(profile =>
                                        profile.Id == reconcileProfile &&
                                        profile.Requirements.Any(
                                            requirement => requirement.Id == reconcileRequirement &&
                                                           requirement.State == "Ready")) &&
                !repairSession.Entries.Any(entry => entry.CatalogId == catalog.CatalogId &&
                                                    entry.ProfileId == reconcileProfile &&
                                                    entry.RequirementId == reconcileRequirement &&
                                                    IsPending(entry.Reason)))
            {
                repairSession.Remove(catalog.CatalogId, reconcileProfile, reconcileRequirement!);
            }

            return ApplyRepair(report, repairSession, includeInterrupted);
        }

        void Record(string profileId, ToolRequirement requirement, PreparationStage stage,
                    PreparationReason reason = PreparationReason.None)
        {
            repairSession?.Set(new(catalog.CatalogId, profileId, requirement.Id,
                                   SetupRecipe.Fingerprint(requirement), stage, reason));
        }

        void Finish(string profileId, string requirementId, RecipeStepStatus status,
                    PreparationReason reason)
        {
            if (status is RecipeStepStatus.Completed or RecipeStepStatus.AlreadyReady)
            {
                repairSession?.Remove(catalog.CatalogId, profileId, requirementId);
            }
            else if (status == RecipeStepStatus.Failed)
            {
                Record(profileId,
                       profiles[profileId].Requirements.Single(item => item.Id == requirementId),
                       PreparationStage.Finished, reason);
            }

            steps.Add(new(profileId, requirementId, status, reason));
        }

        void Report(string? profileId, string? requirementId, PreparationStage stage, string? artifactId = null)
        {
            try
            {
                progress?.Report(new(runId, profileId, requirementId, stage, steps.Count,
                                     plan.Length, artifactId));
            }
            catch (Exception)
            {
                // A synchronous UI observer must not interrupt installation or skip validation.
                log?.Write(LogLevel.Warning, "environment",
                           () => "Preparation progress observer failed.");
            }
        }
    }

    private static ToolRequirement[] Order(EnvironmentProfile profile)
    {
        var byId = profile.Requirements.ToDictionary(item => item.Id);
        var visited = new HashSet<string>();
        var result = new List<ToolRequirement>();
        void Visit(ToolRequirement requirement)
        {
            if (!visited.Add(requirement.Id))
            {
                return;
            }

            foreach (var id in requirement.DependsOn)
            {
                Visit(byId[id]);
            }

            result.Add(requirement);
        }

        foreach (var requirement in profile.Requirements.Where(item => item.Required))
        {
            Visit(requirement);
        }

        return result.ToArray();
    }
}

/// <summary>Explicit trusted executable/arguments, bounded lifetime and discarded child
/// output.</summary>
public static class RecipeProcess
{
    public static async Task<RecipeExecutionStatus> ExecuteAsync(
        string executable, IEnumerable<string> arguments, TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (!Path.IsPathFullyQualified(executable) || timeout <= TimeSpan.Zero ||
            timeout > TimeSpan.FromHours(2))
        {
            throw new ArgumentException(
                "Recipe process requires an absolute executable and bounded timeout.");
        }

        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        using var process = new Process { StartInfo = new ProcessStartInfo(executable) {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true,
            RedirectStandardError = true
        } };
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            process.Start();
        }
        catch (Win32Exception error) when (error.NativeErrorCode is 5 or 740)
        {
            return RecipeExecutionStatus.PrivilegeRequired;
        }

        try
        {
            await Task.WhenAll(DrainAsync(process.StandardOutput),
                               DrainAsync(process.StandardError),
                               process.WaitForExitAsync(deadline.Token));
            return process.ExitCode switch {
                0 => RecipeExecutionStatus.Completed,
                3010 or
                1641 => RecipeExecutionStatus.RestartRequired,
                740 => RecipeExecutionStatus.PrivilegeRequired,
                _ => RecipeExecutionStatus.Failed
            };
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException("Recipe process exceeded its deadline.");
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync(CancellationToken.None);
            }
        }

        async Task DrainAsync(StreamReader reader)
        {
            var buffer = new char[8192];
            var total = 0;
            int read;
            while ((read = await reader.ReadAsync(buffer, deadline.Token)) != 0)
            {
                total += read;
                if (total > 1048576)
                {
                    deadline.Cancel();
                    throw new IOException("Recipe output exceeded its limit.");
                }
            }
        }
    }
}
