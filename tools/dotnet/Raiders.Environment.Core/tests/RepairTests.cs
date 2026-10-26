using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Raiders.Diagnostics;

namespace Raiders.Environment.Core.Tests;

[TestClass]
public sealed class RepairTests
{
    private static readonly MachineIdentity Host = new("windows", "x64");
    private readonly string root =
        Path.Combine(Path.GetTempPath(), "raiders-repair-" + Guid.NewGuid().ToString("N"));
    private readonly HashSet<string> ready = [];
    private readonly List<string> executed = [];
    private string Catalog =>
        File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "environment-profile.json"));

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private RecipeRunner Runner(
        Func<RecipeContext, CancellationToken, Task<RecipeExecutionStatus>>? execute = null,
        string? inconclusive = null, string? stale = null)
    {
        var discovery = new ToolDiscovery();
        var profiles = EnvironmentProbe.ReadCatalog(Catalog).Profiles;
        foreach (var requirement in profiles.SelectMany(profile => profile.Requirements)
                     .DistinctBy(item => item.ProbeId))
        {
            discovery.Register(
                requirement.ProbeId,
                (policy, _) => ValueTask.FromResult(
                    policy.Id == inconclusive ? new DiscoveryResult("failed", [])
                    : ready.Contains(policy.Id)
                        ? new DiscoveryResult(
                              "found",
                              [new(policy.Constraint.Value ??
                                       policy.Constraint.Values?.FirstOrDefault(),
                                   policy.Architecture,
                                   policy.Id == stale ? new string('b', 64) : new string('a', 64))],
                              new string('a', 64))
                        : new DiscoveryResult("absent", [])));
        }

        var recipes = profiles.SelectMany(
            profile => profile.Requirements.Select(
                requirement => new SetupRecipe(requirement.RecipeRef!, profile.Id, requirement.Id,
                                               SetupRecipe.Fingerprint(requirement), [],
                                               async (context, token) =>
                                               {
                                                   executed.Add(context.Requirement.Id);
                                                   if (execute is not null)
                                                   {
                                                       return await execute(context, token);
                                                   }

                                                   ready.Add(context.Requirement.Id);
                                                   return RecipeExecutionStatus.Completed;
                                               })));
        return new(discovery, recipes, new ArtifactAcquisition(Path.Combine(root, "cache")),
                   new TelemetrySession("repair.test"),
                   repair: new RepairPlan(Path.Combine(root, "repair")));
    }

    private void Interrupt(string requirementId, PreparationStage stage,
                           PreparationReason reason = PreparationReason.None)
    {
        var catalog = EnvironmentProbe.ReadCatalog(Catalog);
        var requirement = catalog.Profiles.Single(profile => profile.Id == "python-pipeline")
                              .Requirements.Single(item => item.Id == requirementId);
        Directory.CreateDirectory(Path.Combine(root, "repair"));
        var entry = new RepairRecord(catalog.CatalogId, "python-pipeline", requirementId,
                                     SetupRecipe.Fingerprint(requirement), stage, reason);
        File.WriteAllText(Path.Combine(root, "repair", "repair-state.json"),
                          JsonSerializer.Serialize(new { Version = 1, Entries = new[] { entry } }));
    }

    [TestMethod]
    [DataRow(PreparationStage.Acquiring)]
    [DataRow(PreparationStage.Executing)]
    [DataRow(PreparationStage.Validating)]
    public async Task
    ReopenedInterruptedStepIsVisibleAndOnlyUnhealthyStepsExecute(PreparationStage stage)
    {
        ready.Add("python.interpreter");
        Interrupt("python.build", stage);
        var report = await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual("Needs repair", report.State);
        Assert.IsTrue(report.ReasonCodes.Contains("IncompleteSetup"));

        var result = await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        CollectionAssert.AreEqual(new[] { "python.build", "python.tools" }, executed);
        executed.Clear();
        await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(0, executed.Count);
    }

    [TestMethod]
    public async Task InterruptedButHealthyStepRequiresFreshValidationRatherThanReinstallation()
    {
        ready.UnionWith(["python.interpreter", "python.build", "python.tools"]);
        Interrupt("python.build", PreparationStage.Executing);
        Assert.AreEqual(
            "Needs repair",
            (await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host)).State);
        var result = await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual("Ready", result.Health.State);
        Assert.AreEqual(0, executed.Count);
        Assert.AreEqual(
            "Ready", (await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host)).State);
    }

    [TestMethod]
    public async Task CancellationAfterExecutionRemainsVisibleAcrossInstances()
    {
        using var cancellation = new CancellationTokenSource();
        var runner = Runner((context, _) =>
                            {
                                ready.Add(context.Requirement.Id);
                                cancellation.Cancel();
                                return Task.FromResult(RecipeExecutionStatus.Completed);
                            });
        await Assert.ThrowsExactlyAsync<OperationCanceledException>(
            () => runner.RunAsync(Catalog, ["python-pipeline"], machine: Host,
                                  cancellationToken: cancellation.Token));

        var report = await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.IsTrue(report.ReasonCodes.Contains("IncompleteSetup"));
        await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(1, executed.Count(id => id == "python.interpreter"));
    }

    [TestMethod]
    [DataRow(PreparationReason.RestartRequired)]
    [DataRow(PreparationReason.PrivilegeRequired)]
    public async Task
    PendingActionSurvivesReopenAndNeedsExplicitAcknowledgement(PreparationReason reason)
    {
        ready.UnionWith(["python.interpreter", "python.build", "python.tools"]);
        Interrupt("python.build", PreparationStage.Validating, reason);
        var runner = Runner();
        var blocked = await runner.RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Blocked, blocked.Status);
        Assert.IsTrue((await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host))
                          .ReasonCodes.Contains(reason.ToString()));
        var resolved = await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host,
                                                acknowledgePendingActions: true);
        Assert.AreEqual("Ready", resolved.State);
        Assert.AreEqual(0, executed.Count);
    }

    [TestMethod]
    public async Task AcknowledgedPrivilegeActionCanRetryAnAbsentTool()
    {
        Interrupt("python.interpreter", PreparationStage.Validating,
                  PreparationReason.PrivilegeRequired);
        var report = await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host,
                                              acknowledgePendingActions: true);
        Assert.AreEqual("Missing", report.State);
        var result = await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
    }

    [TestMethod]
    public async Task InconclusiveDiscoveryNeverAuthorizesInterruptedRetryOrAcknowledgement()
    {
        Interrupt("python.interpreter", PreparationStage.Executing,
                  PreparationReason.RestartRequired);
        var runner = Runner(inconclusive: "python.interpreter");
        var report = await runner.ScanAsync(Catalog, ["python-pipeline"], machine: Host,
                                            acknowledgePendingActions: true);
        Assert.AreEqual("Unknown", report.Profiles.Single().Requirements[0].State);
        await runner.RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(0, executed.Count);
        Assert.IsTrue((await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host))
                          .ReasonCodes.Contains("RestartRequired"));
    }

    [TestMethod]
    public async Task FailedStepRetriesWithoutTouchingHealthyPrerequisiteOrUserFiles()
    {
        ready.Add("python.interpreter");
        Directory.CreateDirectory(root);
        var userFile = Path.Combine(root, "user-settings.txt");
        File.WriteAllText(userFile, "keep");
        var failed = await Runner((_, _) => Task.FromResult(RecipeExecutionStatus.Failed))
                         .RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, failed.Status);
        Assert.IsTrue((await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host))
                          .ReasonCodes.Contains("IncompleteSetup"));
        var result = await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual("Ready", result.Health.State);
        Assert.AreEqual(2, executed.Count(id => id == "python.build"));

        Assert.IsFalse(executed.Contains("python.interpreter"));
        Assert.AreEqual("keep", File.ReadAllText(userFile));
    }

    [TestMethod]
    public async Task StaleVenvFingerprintIsRepairedWhileHealthyToolsArePreserved()
    {
        ready.UnionWith(["python.interpreter", "python.build"]);
        var stale = true;
        var discovery = new ToolDiscovery();
        foreach (var requirement in EnvironmentProbe.ReadCatalog(Catalog).Profiles[2].Requirements)
        {
            discovery.Register(
                requirement.ProbeId,
                (policy, _) => ValueTask.FromResult(new DiscoveryResult(
                    "found",
                    [new(policy.Constraint.Value ?? policy.Constraint.Values?.FirstOrDefault(),
                         policy.Architecture,
                         stale && policy.Id == "python.build" ? new string('b', 64)
                                                              : new string('a', 64))],
                    new string('a', 64))));
        }

        var policy = EnvironmentProbe.ReadCatalog(Catalog).Profiles[2].Requirements[1];
        var recipe = new SetupRecipe(policy.RecipeRef!, "python-pipeline", policy.Id,
                                     SetupRecipe.Fingerprint(policy), [],
                                     (_, _) =>
                                     {
                                         stale = false;
                                         executed.Add(policy.Id);
                                         return Task.FromResult(RecipeExecutionStatus.Completed);
                                     });
        var runner = new RecipeRunner(discovery, [recipe],
                                      new ArtifactAcquisition(Path.Combine(root, "cache")),
                                      new TelemetrySession("repair.stale"),
                                      repair: new RepairPlan(Path.Combine(root, "repair")));
        var result = await runner.RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual("Ready", result.Health.State);
        CollectionAssert.AreEqual(new[] { "python.build" }, executed);
    }

    [TestMethod]
    public async Task CorruptJournalFailsClosedAndIsPreserved()
    {
        Interrupt("python.build", PreparationStage.Executing);
        var path = Path.Combine(root, "repair", "repair-state.json");
        File.WriteAllText(path, "broken");
        await Assert.ThrowsExactlyAsync<JsonException>(
            () => Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host));

        Assert.AreEqual("broken", File.ReadAllText(path));
        Assert.AreEqual(0, executed.Count);
    }

    [TestMethod]
    public async Task InstallerRestartResultIsPersistedEvenWhenCancellationArrivesAtReturn()
    {
        using var cancellation = new CancellationTokenSource();
        var runner = Runner((context, _) =>
                            {
                                ready.Add(context.Requirement.Id);
                                cancellation.Cancel();
                                return Task.FromResult(RecipeExecutionStatus.RestartRequired);
                            });
        await Assert.ThrowsExactlyAsync<OperationCanceledException>(
            () => runner.RunAsync(Catalog, ["python-pipeline"], machine: Host,
                                  cancellationToken: cancellation.Token));

        var report = await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.IsTrue(report.ReasonCodes.Contains("RestartRequired"));
    }

    [TestMethod]
    public async Task FailedExecutionCannotUnlockDependentJobsEvenIfFilesLookHealthy()
    {
        var result = await Runner((context, _) =>
                                  {
                                      ready.Add(context.Requirement.Id);
                                      return Task.FromResult(RecipeExecutionStatus.Failed);
                                  })
                         .RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, result.Status);
        CollectionAssert.AreEqual(new[] { "python.interpreter" }, executed);
        Assert.IsTrue(result.Health.ReasonCodes.Contains("IncompleteSetup"));
        var next = await Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host);
        Assert.AreEqual("Ready", next.Health.State);
        Assert.AreEqual(1, executed.Count(id => id == "python.interpreter"));
    }

    [TestMethod]
    public async Task ReconciliationDoesNotClearUnselectedProfiles()
    {
        Interrupt("python.build", PreparationStage.Validating, PreparationReason.RestartRequired);
        var runner = Runner();
        await runner.ScanAsync(Catalog, ["managed-tooling"], machine: Host,
                               acknowledgePendingActions: true);
        Assert.IsTrue((await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host))
                          .ReasonCodes.Contains("RestartRequired"));
    }

    [TestMethod]
    public async Task SeparateInstancesCannotModifyTheSameRepairStateConcurrently()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = Runner(async (context, _) =>
                           {
                               started.TrySetResult();
                               await release.Task;
                               ready.Add(context.Requirement.Id);
                               return RecipeExecutionStatus.Completed;
                           })
                        .RunAsync(Catalog, ["python-pipeline"], machine: Host);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
        try
        {
            await Assert.ThrowsExactlyAsync<IOException>(
                () => Runner().RunAsync(Catalog, ["python-pipeline"], machine: Host));
        }
        finally
        {
            release.SetResult();
            await first;
        }

        Assert.AreEqual(
            "Ready", (await Runner().ScanAsync(Catalog, ["python-pipeline"], machine: Host)).State);
    }

    [TestMethod]
    public async Task CorruptCacheIsReplacedOnlyAfterSuccessfulVerifiedAcquisition()
    {
        Directory.CreateDirectory(root);
        var source = Path.Combine(root, "source.bin");
        var bytes = Encoding.UTF8.GetBytes("approved artifact");
        var hash = Convert.ToHexStringLower(SHA256.HashData(bytes));
        var cache = Path.Combine(root, "cache", hash);
        Directory.CreateDirectory(cache);
        var destination = Path.Combine(cache, "tool.bin");
        File.WriteAllText(destination, "corrupt");
        var orphan = Path.Combine(cache, "interrupted.partial");
        File.WriteAllText(orphan, "incomplete");
        var artifact = new SetupArtifact("test", new Uri(source), "tool.bin", hash);
        var acquisition = new ArtifactAcquisition(Path.Combine(root, "cache"));
        await Assert.ThrowsExactlyAsync<FileNotFoundException>(
            () => acquisition.AcquireAsync(artifact, repairCorruptCache: true));

        Assert.AreEqual("corrupt", File.ReadAllText(destination));
        File.WriteAllBytes(source, bytes);
        await acquisition.AcquireAsync(artifact, repairCorruptCache: true);
        CollectionAssert.AreEqual(bytes, File.ReadAllBytes(destination));
        Assert.AreEqual("incomplete", File.ReadAllText(orphan));
    }
}
