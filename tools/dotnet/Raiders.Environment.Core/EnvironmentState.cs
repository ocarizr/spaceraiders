namespace Raiders.Environment.Core;

public enum PreparationStatus
{
    Completed,
    Blocked,
    Failed
}

public enum RecipeStepStatus
{
    AlreadyReady,
    Completed,
    Blocked,
    Failed
}

public enum PreparationReason
{
    None,
    DependencyNotReady,
    RecipeUnavailable,
    PolicyChanged,
    ProbeInconclusive,
    UnsupportedHost,
    ArtifactRejected,
    AcquisitionFailed,
    ExecutionFailed,
    ValidationFailed,
    RestartRequired,
    PrivilegeRequired
}

public enum PreparationStage
{
    Checking,
    Acquiring,
    Executing,
    Validating,
    Finished,
    Cancelled
}

public enum RecipeExecutionStatus
{
    Completed,
    Failed,
    RestartRequired,
    PrivilegeRequired
}

public sealed record RecipeStepResult(string ProfileId, string RequirementId,
                                      RecipeStepStatus Status, PreparationReason Reason);
public sealed record PreparationProgress(string RunId, string? ProfileId, string? RequirementId,
                                         PreparationStage Stage, int CompletedSteps, int TotalSteps,
                                         string? ArtifactId = null);

public sealed record EnvironmentState(string RunId, PreparationStatus Status,
                                      EnvironmentHealth Health, RecipeStepResult[] Steps)
{
    // Pending installer actions are current-session evidence, never persisted Ready receipts.
    internal static EnvironmentHealth ApplyActions(
        EnvironmentHealth report,
        IReadOnlyDictionary<(string Profile, string Requirement), PreparationReason> actions)
    {
        if (report.Machine != new MachineIdentity("windows", "x64"))
        {
            return report;
        }

        var profiles = new Dictionary<string, ProfileHealth>();
        foreach (var original in report.Profiles)
        {
            var requirements =
                original.Requirements
                    .Select(
                        requirement =>
                        {
                            if (!actions.TryGetValue((original.Id, requirement.Id), out var reason))
                            {
                                return requirement;
                            }

                            var evidence = requirement.Evidence.Last()
                                               with { Outcome = "mismatch", Complete = true,
                                                      ObservedAt = report.CompletedAt };
                            return requirement with { State = "Needs repair",
                                                      ReasonCodes = [reason.ToString()],
                                                      Evidence =
                                                          [..requirement.Evidence, evidence] };
                        })
                    .ToArray();
            var dependencies =
                original.Dependencies
                    .Select(dependency =>
                                dependency with { State = profiles[dependency.ProfileId].State })
                    .ToArray();
            var required = requirements.Where(requirement => requirement.Required).ToArray();
            var state = Aggregate(required.Select(requirement => requirement.State)
                                      .Concat(dependencies.Select(dependency => dependency.State)));
            var reasons = required.SelectMany(requirement => requirement.ReasonCodes).ToList();
            if (dependencies.Any(dependency => dependency.State != "Ready"))
            {
                reasons.Add("DependencyNotReady");
            }

            profiles.Add(original.Id, original with {
                State = state,
                ReasonCodes = state == "Ready" ? [] : reasons.Distinct().Order().ToArray(),
                Requirements = requirements, Dependencies = dependencies
            });
        }

        var all = report.ResolvedProfiles.Select(id => profiles[id]).ToArray();
        var aggregate = Aggregate(all.Select(profile => profile.State));
        return report with {
            State = aggregate,
            ReasonCodes =
                aggregate == "Ready"
                    ? []
                    : all.SelectMany(profile => profile.ReasonCodes).Distinct().Order().ToArray(),
            Profiles = all
        };
    }

    private static string Aggregate(IEnumerable<string> states)
    {
        var values = states.ToHashSet();
        string[] priority =
            ["Unsupported", "Needs repair", "Missing", "Checking", "Unknown", "Ready"];
        return priority.First(values.Contains);
    }
}
