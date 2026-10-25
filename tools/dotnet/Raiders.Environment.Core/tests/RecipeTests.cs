using System.Collections.Concurrent;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Raiders.Diagnostics;

namespace Raiders.Environment.Core.Tests;

[TestClass]
public sealed class RecipeTests
{
    private static readonly MachineIdentity Host = new("windows", "x64");
    private static readonly string Hash = new('a', 64);
    private readonly string temporary =
        Path.Combine(Path.GetTempPath(), "raiders-recipes-" + Guid.NewGuid().ToString("N"));
    private readonly HashSet<string> ready = [];
    private readonly List<string> installed = [];

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(temporary))
        {
            Directory.Delete(temporary, recursive: true);
        }
    }

    private static string Catalog()
    {
        return File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "environment-profile.json"));
    }

    private ToolDiscovery Discovery(Func<ToolRequirement, string>? state = null)
    {
        var discovery = new ToolDiscovery();
        foreach (var item in EnvironmentProbe.ReadCatalog(Catalog())
                     .Profiles.SelectMany(profile => profile.Requirements)
                     .DistinctBy(item => item.ProbeId))
        {
            discovery.Register(
                item.ProbeId,
                (requirement, _) =>
                {
                    var status = state?.Invoke(requirement) ??
                                 (ready.Contains(requirement.Id) ? "found" : "absent");
                    return ValueTask.FromResult(
                        status == "found"
                            ? new DiscoveryResult(
                                  "found", [new(requirement.Constraint.Value ??
                                                    requirement.Constraint.Values?.FirstOrDefault(),
                                                requirement.Architecture, Hash)],
                                  Hash)
                            : new DiscoveryResult(status, []));
                });
        }

        return discovery;
    }

    private SetupRecipe[] Recipes(
        Func<RecipeContext, CancellationToken, Task<RecipeExecutionStatus>>? execute = null)
    {
        return EnvironmentProbe.ReadCatalog(Catalog())
            .Profiles
            .SelectMany(profile => profile.Requirements.Select(
                            requirement => new SetupRecipe(
                                requirement.RecipeRef!, profile.Id, requirement.Id,
                                SetupRecipe.Fingerprint(requirement), [],
                                execute ?? ((context, token) =>
                                            {
                                                installed.Add(context.Requirement.Id);
                                                ready.Add(context.Requirement.Id);
                                                return Task.FromResult(
                                                    RecipeExecutionStatus.Completed);
                                            }))))
            .ToArray();
    }

    private RecipeRunner Runner(IEnumerable<SetupRecipe>? recipes = null,
                                ToolDiscovery? discovery = null, StringWriter? sink = null)
    {
        var telemetry = new TelemetrySession("recipe.test");
        return new(discovery ?? Discovery(), recipes ?? Recipes(),
                   new ArtifactAcquisition(Path.Combine(temporary, "cache")), telemetry,
                   sink is null ? null : new Log(telemetry, sink));
    }

    [TestMethod]
    public async Task SelectedPythonPrerequisitesExecuteInOrderRegardlessOfDeclarationOrder()
    {
        var node = JsonNode.Parse(Catalog())!;
        var requirements = node["profiles"]![2]!["requirements"]!.AsArray();
        node["profiles"]![2]!["requirements"] =
            new JsonArray(requirements.Reverse().Select(item => item!.DeepClone()).ToArray());

        var recipes =
            Recipes()
                .Select(recipe =>
                        {
                            var requirement =
                                EnvironmentProbe.ReadCatalog(node.ToJsonString())
                                    .Profiles.Single(profile => profile.Id == recipe.ProfileId)
                                    .Requirements.Single(item => item.Id == recipe.RequirementId);
                            return recipe with { PolicySha256 =
                                                     SetupRecipe.Fingerprint(requirement) };
                        })
                .ToArray();
        var result =
            await Runner(recipes).RunAsync(node.ToJsonString(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        Assert.AreEqual("Ready", result.Health.State);
        CollectionAssert.AreEqual(new[] { "python.interpreter", "python.build", "python.tools" },
                                  installed);
        CollectionAssert.AreEqual(new[] { "python-pipeline" }, result.Health.ResolvedProfiles);
    }

    [TestMethod]
    public async Task HealthyProfileIsNoOpAndDoesNotCreateArtifactCache()
    {
        foreach (var requirement in EnvironmentProbe.ReadCatalog(Catalog())
                     .Profiles[2]
                     .Requirements)
        {
            ready.Add(requirement.Id);
        }

        var result = await Runner().RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        Assert.IsTrue(result.Steps.All(step => step.Status == RecipeStepStatus.AlreadyReady));

        Assert.AreEqual(0, installed.Count);
        Assert.IsFalse(Directory.Exists(temporary));
    }

    [TestMethod]
    public async Task ReopeningRechecksHealthAndSkipsHealthySteps()
    {
        var runner = Runner();
        await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        var second = await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(3, installed.Count);
        Assert.IsTrue(second.Steps.All(step => step.Status == RecipeStepStatus.AlreadyReady));

        ready.Remove("python.tools");
        var third = await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, third.Status);
        Assert.AreEqual(4, installed.Count);
        Assert.AreEqual("python.tools", installed.Last());
    }

    [TestMethod]
    public async Task DesignerPreparationOmitsOptionalToolsAndDeveloperProfiles()
    {
        var result = await Runner().RunAsync(Catalog(), ["designer-tools"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        CollectionAssert.AreEqual(new[] { "designer.sdk" }, installed);
        Assert.AreEqual(1, result.Steps.Length);
        Assert.AreEqual(
            "Missing", result.Health.Profiles[0].Requirements.Single(item => !item.Required).State);
    }

    [TestMethod]
    public async Task ProfilePrerequisitesRunBeforeConsumerRecipes()
    {
        var node = JsonNode.Parse(Catalog())!;
        node["profiles"]![1]!["depends_on"] = new JsonArray("native-development");
        var result =
            await Runner().RunAsync(node.ToJsonString(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        Assert.IsTrue(installed.IndexOf("managed.sdk") > installed.IndexOf("native.ninja"));
        CollectionAssert.AreEqual(new[] { "native-development", "managed-tooling" },
                                  result.Health.ResolvedProfiles);
    }

    [TestMethod]
    public async Task FailedPrerequisiteBlocksDependentsButIndependentRecipesContinue()
    {
        var recipes = Recipes((context, _) =>
                              {
                                  installed.Add(context.Requirement.Id);
                                  if (context.Requirement.Id == "python.interpreter")
                                  {
                                      return Task.FromResult(RecipeExecutionStatus.Failed);
                                  }

                                  ready.Add(context.Requirement.Id);
                                  return Task.FromResult(RecipeExecutionStatus.Completed);
                              });
        var result = await Runner(recipes).RunAsync(
            Catalog(), ["python-pipeline", "managed-tooling"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, result.Status);
        Assert.IsFalse(installed.Contains("python.build"));
        Assert.IsFalse(installed.Contains("python.tools"));
        Assert.IsTrue(installed.Contains("managed.packages"));
        Assert.IsTrue(
            result.Steps.Where(step => step.RequirementId is "python.build" or "python.tools")
                .All(step => step.Reason == PreparationReason.DependencyNotReady));
    }

    [TestMethod]
    public async Task ZeroExitWithoutHealthyPostProbeIsValidationFailure()
    {
        var recipes = Recipes((_, _) => Task.FromResult(RecipeExecutionStatus.Completed));
        var result = await Runner(recipes).RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, result.Status);
        Assert.AreEqual(PreparationReason.ValidationFailed, result.Steps[0].Reason);
        Assert.AreEqual("Missing", result.Health.State);
    }

    [TestMethod]
    [DataRow(RecipeExecutionStatus.RestartRequired, "RestartRequired")]
    [DataRow(RecipeExecutionStatus.PrivilegeRequired, "PrivilegeRequired")]
    public async Task
    PendingInstallerActionCannotBeHiddenByReadyProbe(RecipeExecutionStatus outcome, string reason)
    {
        var recipes = Recipes((context, _) =>
                              {
                                  installed.Add(context.Requirement.Id);
                                  ready.Add(context.Requirement.Id);
                                  return Task.FromResult(outcome);
                              });
        var runner = Runner(recipes);
        var result = await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Blocked, result.Status);
        Assert.AreEqual("Needs repair", result.Health.State);
        CollectionAssert.Contains(result.Health.ReasonCodes, reason);
        Assert.AreEqual(1, installed.Count);
        Assert.AreEqual(PreparationReason.DependencyNotReady, result.Steps[1].Reason);
        var output = System.Environment.GetEnvironmentVariable("RAIDERS_PREPARATION_HEALTH_OUTPUT");
        if (output is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
            await File.WriteAllTextAsync(output + "." + reason + ".json", result.Health.ToJson());
        }

        var second = await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(PreparationStatus.Blocked, second.Status);
        CollectionAssert.Contains(second.Health.ReasonCodes, reason);
        Assert.AreEqual(1, installed.Count);
        var unsupported =
            await runner.RunAsync(Catalog(), ["python-pipeline"], machine: new("windows", "arm64"));
        Assert.AreEqual("Unsupported", unsupported.Health.State);
    }

    [TestMethod]
    [DataRow("missing", PreparationReason.RecipeUnavailable)]
    [DataRow("policy", PreparationReason.PolicyChanged)]
    [DataRow("id", PreparationReason.RecipeUnavailable)]
    public async Task MissingOrOutdatedRecipeCannotExecute(string mode, PreparationReason reason)
    {
        var recipes = mode == "missing"
                          ? []
                          : Recipes()
                                .Select(recipe => recipe with {
                                    Id = mode == "id" ? "other-recipe" : recipe.Id,
                                    PolicySha256 =
                                        mode == "policy" ? new string('b', 64) : recipe.PolicySha256
                                })
                                .ToArray();
        var result = await Runner(recipes).RunAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(PreparationStatus.Blocked, result.Status);
        Assert.AreEqual(reason, result.Steps[0].Reason);
        Assert.AreEqual(0, installed.Count);
    }

    [TestMethod]
    public async Task UnknownDetectionNeverAuthorizesInstallation()
    {
        var result = await Runner(discovery: Discovery(
                                      _ => "failed"))
                         .RunAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(PreparationStatus.Blocked, result.Status);
        Assert.AreEqual(PreparationReason.ProbeInconclusive, result.Steps[0].Reason);
        Assert.AreEqual(0, installed.Count);
    }

    [TestMethod]
    public async Task UnsupportedHostCannotExecuteRecipes()
    {
        var result = await Runner().RunAsync(Catalog(), ["managed-tooling"],
                                             machine: new("windows", "arm64"));
        Assert.AreEqual("Unsupported", result.Health.State);
        Assert.IsTrue(result.Steps.All(step => step.Reason == PreparationReason.UnsupportedHost));

        Assert.AreEqual(0, installed.Count);
    }

    [TestMethod]
    public async Task ProgressHasRunIdentityStagesAndCountsWithoutPrivatePaths()
    {
        var progress = new Recorder();
        using var log = new StringWriter();
        var result =
            await Runner(sink: log).RunAsync(Catalog(), ["python-pipeline"], progress, Host);
        Assert.IsTrue(
            progress.Events.All(item => item.RunId == result.RunId && item.TotalSteps == 3));

        Assert.AreEqual(PreparationStage.Finished, progress.Events.Last().Stage);
        Assert.AreEqual(3, progress.Events.Last().CompletedSteps);
        CollectionAssert.AreEqual(
            new[] { PreparationStage.Checking, PreparationStage.Executing,
                    PreparationStage.Validating },
            progress.Events.Where(item => item.RequirementId == "python.build")
                .Select(item => item.Stage)
                .ToArray());

        Assert.DoesNotContain(temporary, JsonSerializer.Serialize(progress.Events));
        Assert.Contains("duration_us", log.ToString());
    }

    [TestMethod]
    public async Task ProgressObserverFailureDoesNotSkipPostValidation()
    {
        var result = await Runner().RunAsync(Catalog(), ["managed-tooling"],
                                             new Recorder(throwOnReport: true), Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        Assert.AreEqual("Ready", result.Health.State);
    }

    [TestMethod]
    public async Task CancellationStopsDependentsAndReleasesRunnerForNextRun()
    {
        using var cancellation = new CancellationTokenSource();
        var first = true;
        var recipes = Recipes((context, token) =>
                              {
                                  if (first)
                                  {
                                      first = false;
                                      cancellation.Cancel();
                                      token.ThrowIfCancellationRequested();
                                  }

                                  ready.Add(context.Requirement.Id);
                                  return Task.FromResult(RecipeExecutionStatus.Completed);
                              });
        var progress = new Recorder();
        var runner = Runner(recipes);
        await Assert.ThrowsAsync<OperationCanceledException>(
            () => runner.RunAsync(Catalog(), ["python-pipeline"], progress, Host,
                                  cancellation.Token));

        Assert.AreEqual(PreparationStage.Cancelled, progress.Events.Last().Stage);
        Assert.AreEqual(0, ready.Count);
        Assert.AreEqual(
            PreparationStatus.Completed,
            (await runner.RunAsync(Catalog(), ["python-pipeline"], machine: Host)).Status);
    }

    [TestMethod]
    public async Task ConcurrentRunsOnOneRunnerDoNotInstallTwice()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var recipes = Recipes(async (context, token) =>
                              {
                                  installed.Add(context.Requirement.Id);
                                  started.TrySetResult();
                                  await release.Task.WaitAsync(token);
                                  ready.Add(context.Requirement.Id);
                                  return RecipeExecutionStatus.Completed;
                              });
        var runner = Runner(recipes);
        var first = runner.RunAsync(Catalog(), ["managed-tooling"], machine: Host);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = runner.RunAsync(Catalog(), ["managed-tooling"], machine: Host);
        release.SetResult();
        var results = await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.AreEqual(2, installed.Count);
        Assert.IsTrue(results.All(result => result.Status == PreparationStatus.Completed));

        Assert.IsTrue(results[1].Steps.All(step => step.Status == RecipeStepStatus.AlreadyReady));
    }

    [TestMethod]
    public async Task ArtifactMismatchPreventsExecutionAndDoesNotReplaceInstalledFiles()
    {
        Directory.CreateDirectory(temporary);
        var source = Path.Combine(temporary, "installer.exe");
        await File.WriteAllTextAsync(source, "incorrect artifact");
        var marker = Path.Combine(temporary, "last-good.txt");
        await File.WriteAllTextAsync(marker, "preserve");
        var recipes =
            Recipes()
                .Select(recipe => recipe with { Artifacts = [new("installer", new Uri(source),
                                                                 "installer.exe", Hash)] })
                .ToArray();
        var result = await Runner(recipes).RunAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, result.Status);
        Assert.AreEqual(PreparationReason.ArtifactRejected, result.Steps[0].Reason);
        Assert.AreEqual(0, installed.Count);
        Assert.AreEqual("preserve", await File.ReadAllTextAsync(marker));
        Assert.IsFalse(
            Directory.EnumerateFiles(temporary, "*.partial", SearchOption.AllDirectories).Any());
    }

    private sealed class Recorder(bool throwOnReport = false) : IProgress<PreparationProgress>
    {
        public ConcurrentQueue<PreparationProgress> Events { get; } = new();

        public void Report(PreparationProgress value)
        {
            if (throwOnReport)
            {
                throw new InvalidOperationException("private observer failure");
            }

            Events.Enqueue(value);
        }
    }

    private static string PowerShell()
    {
        return System.Environment.GetEnvironmentVariable("PATH")!.Split(Path.PathSeparator)
            .Select(directory => Path.Combine(directory, "pwsh.exe"))
            .First(File.Exists);
    }

    [TestMethod]
    public async Task VerifiedArtifactFeedsIsolatedInstallerAndRecheckMakesSecondRunNoOp()
    {
        Directory.CreateDirectory(temporary);
        var script = Path.Combine(temporary, "installer.ps1");
        var marker = Path.Combine(temporary, "installed.txt");
        await File.WriteAllTextAsync(
            script, "param([string]$Marker)\n[IO.File]::WriteAllText($Marker, 'fixture')\n");
        var artifact = new SetupArtifact(
            "installer", new Uri(script), "installer.ps1",
            Convert.ToHexStringLower(SHA256.HashData(await File.ReadAllBytesAsync(script))));
        var recipe =
            Recipes().Single(item => item.RequirementId == "designer.sdk")
                with { Artifacts = [artifact],
                       ExecuteAsync = async (context, token) =>
                       {
                           var path = context.Artifacts["installer"].Path;
                           Assert.AreNotEqual(script, path);
                           Assert.AreEqual(artifact.Sha256,
                                           Convert.ToHexStringLower(SHA256.HashData(
                                               await File.ReadAllBytesAsync(path, token))));
                           installed.Add(context.Requirement.Id);
                           var outcome = await RecipeProcess.ExecuteAsync(
                               PowerShell(),
                               [
                                   "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                   "-File", path, marker
                               ],
                               TimeSpan.FromSeconds(10), token);
                           if (outcome == RecipeExecutionStatus.Completed &&
                               await File.ReadAllTextAsync(marker, token) == "fixture")
                           {
                               ready.Add(context.Requirement.Id);
                           }

                           return outcome;
                       } };
        var progress = new Recorder();
        var runner = Runner([recipe]);
        var result = await runner.RunAsync(Catalog(), ["designer-tools"], progress, Host);
        Assert.AreEqual(PreparationStatus.Completed, result.Status);
        Assert.IsTrue(progress.Events.Any(item => item.Stage == PreparationStage.Acquiring &&
                                                  item.ArtifactId == "installer"));

        Assert.AreEqual(
            PreparationStatus.Completed,
            (await runner.RunAsync(Catalog(), ["designer-tools"], machine: Host)).Status);
        Assert.AreEqual(1, installed.Count);
    }

    [TestMethod]
    public async Task InstallerFailureDoesNotExposeExceptionOrBecomeSuccessWhenProbeIsReady()
    {
        using var log = new StringWriter();
        var recipes = Recipes((context, _) =>
                              {
                                  ready.Add(context.Requirement.Id);
                                  throw new IOException("private user path and credentials");
                              });
        var result =
            await Runner(recipes, sink: log).RunAsync(Catalog(), ["designer-tools"], machine: Host);
        Assert.AreEqual(PreparationStatus.Failed, result.Status);
        Assert.AreEqual(PreparationReason.ExecutionFailed, result.Steps[0].Reason);
        Assert.DoesNotContain("credentials", log.ToString());
        Assert.DoesNotContain("credentials", JsonSerializer.Serialize(result));
    }

    [TestMethod]
    public async Task ProcessAdapterEnforcesTimeout()
    {
        await Assert.ThrowsAsync<TimeoutException>(
            () => RecipeProcess.ExecuteAsync(
                PowerShell(),
                ["-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 60"],
                TimeSpan.FromSeconds(1)));
    }

    [TestMethod]
    [DataRow(0, RecipeExecutionStatus.Completed)]
    [DataRow(3010, RecipeExecutionStatus.RestartRequired)]
    [DataRow(740, RecipeExecutionStatus.PrivilegeRequired)]
    [DataRow(42, RecipeExecutionStatus.Failed)]
    public async Task
    ProcessAdapterMapsExitCodesAndKeepsArgumentsSeparate(int code, RecipeExecutionStatus expected)
    {
        Directory.CreateDirectory(temporary);
        var script = Path.Combine(temporary, "fake installer.ps1");
        var marker = Path.Combine(temporary, "installed marker.txt");
        await File.WriteAllTextAsync(
            script, "param([string]$Marker, [int]$Result)\n[IO.File]::WriteAllText($Marker, " +
                        "'fixture')\nexit $Result\n");
        var result = await RecipeProcess.ExecuteAsync(PowerShell(),
                                                      [
                                                          "-NoProfile", "-NonInteractive",
                                                          "-ExecutionPolicy", "Bypass", "-File",
                                                          script, marker, code.ToString()
                                                      ],
                                                      TimeSpan.FromSeconds(10));
        Assert.AreEqual(expected, result);
        Assert.AreEqual("fixture", await File.ReadAllTextAsync(marker));
    }

    [TestMethod]
    public async Task ProcessAdapterHonorsCancellationAndTerminatesOwnedChild()
    {
        Directory.CreateDirectory(temporary);
        var script = Path.Combine(temporary, "waiting.ps1");
        var marker = Path.Combine(temporary, "pid.txt");
        await File.WriteAllTextAsync(script,
                                     "param([string]$Marker)\n[IO.File]::WriteAllText($" +
                                         "Marker, [string]$PID)\nStart-Sleep -Seconds 60\n");
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var execution =
            RecipeProcess.ExecuteAsync(PowerShell(),
                                       [
                                           "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                                           "Bypass", "-File", script, marker
                                       ],
                                       TimeSpan.FromSeconds(30), cancellation.Token);
        while (!File.Exists(marker))
        {
            await Task.Delay(20, cancellation.Token);
        }

        var pid = int.Parse(await File.ReadAllTextAsync(marker));
        cancellation.Cancel();
        await Assert.ThrowsAsync<OperationCanceledException>(() => execution);

        Assert.Throws<ArgumentException>(() => Process.GetProcessById(pid));
    }
}
