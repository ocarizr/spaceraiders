using System.Diagnostics;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Raiders.Diagnostics;
using Raiders.Environment.Core;

namespace Raiders.Environment.Setup.Tests;

[TestClass]
public sealed class HeadlessTests
{
    private readonly string root =
        Path.Combine(Path.GetTempPath(), "raiders-headless-" + Guid.NewGuid().ToString("N"));

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private sealed class Backend : ISetupBackend
    {
        private readonly RecipeRunner runner;
        public string CatalogJson {
            get;
        } = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "environment-profile.json"));
        public bool SupportsRepair => true;
        public HashSet<string> Ready { get; } = [];
        public List<string> Installed { get; } = [];
        public RecipeExecutionStatus Outcome = RecipeExecutionStatus.Completed;
        public bool Fail;
        public bool Wait;

        public Backend(string root)
        {
            var discovery = new ToolDiscovery();
            var profiles = EnvironmentProbe.ReadCatalog(CatalogJson).Profiles;
            foreach (var requirement in profiles.SelectMany(profile => profile.Requirements)
                         .DistinctBy(item => item.ProbeId))
            {
                discovery.Register(
                    requirement.ProbeId,
                    (policy, _) => ValueTask.FromResult(
                        Ready.Contains(policy.Id)
                            ? new DiscoveryResult(
                                  "found", [new(policy.Constraint.Value ??
                                                    policy.Constraint.Values?.FirstOrDefault(),
                                                policy.Architecture, new string('a', 64),
                                                @"C:\private\secret\tool.exe")],
                                  new string('a', 64))
                            : new DiscoveryResult("absent", [])));
            }

            var recipes =
                profiles.SelectMany(profile => profile.Requirements.Select(
                                        requirement => new SetupRecipe(
                                            requirement.RecipeRef!, profile.Id, requirement.Id,
                                            SetupRecipe.Fingerprint(requirement), [],
                                            (context, _) =>
                                            {
                                                Installed.Add(context.Requirement.Id);
                                                Ready.Add(context.Requirement.Id);
                                                return Task.FromResult(Outcome);
                                            })));
            runner = new(discovery, recipes, new ArtifactAcquisition(Path.Combine(root, "cache")),
                         new TelemetrySession("headless.test"),
                         repair: new RepairPlan(Path.Combine(root, "repair")));
        }

        public bool HasRecipe(string profileId, ToolRequirement requirement)
        {
            return true;
        }

        public async Task<EnvironmentHealth> ScanAsync(string[] selected, CancellationToken token)
        {
            if (Fail)
            {
                throw new IOException("secret/path/token=do-not-print");
            }

            if (Wait)
            {
                await Task.Delay(Timeout.Infinite, token);
            }

            return await runner.ScanAsync(CatalogJson, selected, cancellationToken: token);
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
    }

    private async Task<(int Code, JsonDocument Json, string Output, string Error)> Run(
        string[] args, Backend? backend = null, CancellationToken token = default)
    {
        using var output = new StringWriter();
        using var error = new StringWriter();
        var code = await HeadlessCommands.RunAsync(
            args, output, error,
            (path, _, _) =>
            {
                Assert.AreEqual(root, path);
                return backend ?? throw new AssertFailedException(
                                      "Invalid arguments must not create a backend.");
            },
            token);
        var text = output.ToString();
        var json = JsonDocument.Parse(text);
        Assert.AreEqual(1, json.RootElement.GetProperty("schema_version").GetInt32());
        Assert.AreEqual(code, json.RootElement.GetProperty("exit_code").GetInt32());
        Assert.AreEqual(1, text.Split('\n', StringSplitOptions.RemoveEmptyEntries).Length);
        return (code, json, text, error.ToString());
    }

    private string[] Args(string command, params string[] extra)
    {
        return [command, "--state-root", root, ..extra];
    }

    [TestMethod]
    [DataRow("--help")]
    [DataRow("help")]
    public async Task HelpNeedsNoBackendAndWritesOnlyOneJsonDocument(string command)
    {
        var result = await Run([command]);
        Assert.AreEqual(0, result.Code);
        Assert.IsFalse(Directory.Exists(root));
        Assert.AreEqual(3, result.Json.RootElement.GetProperty("usage").GetArrayLength());
    }

    [TestMethod]
    [DataRow("doctor")]
    [DataRow("prepare")]
    [DataRow("invalid")]
    public async Task MissingSelectionOrInvalidCommandCannotCreateBackend(string command)
    {
        var result = await Run([command]);
        Assert.AreEqual(64, result.Code);
        Assert.IsFalse(Directory.Exists(root));
    }

    [TestMethod]
    [DataRow("--state-root", "relative")]
    [DataRow("--timeout-seconds", "0")]
    [DataRow("--timeout-seconds", "7201")]
    [DataRow("--timeout-seconds", "NaN")]
    [DataRow("--unknown", "secret")]
    public async Task InvalidOptionsNeverInspectOrInstall(string option, string value)
    {
        var result = await Run(["doctor", "--profile", "python-pipeline", option, value]);
        Assert.AreEqual(64, result.Code);
        Assert.IsFalse(result.Output.Contains("secret", StringComparison.Ordinal));
    }

    [TestMethod]
    public async Task PrepareRequiresExplicitConsentAndDoctorCannotAcceptInstallConsent()
    {
        Assert.AreEqual(64, (await Run(Args("prepare", "--profile", "python-pipeline"))).Code);
        Assert.AreEqual(64,
                        (await Run(Args("doctor", "--profile", "python-pipeline", "--yes"))).Code);
        Assert.AreEqual(64, (await Run(Args("prepare", "--profile", "python-pipeline", "--yes",
                                            "--acknowledge-actions")))
                                .Code);
    }

    [TestMethod]
    public async Task UnknownProfileNeverInstallsAndProfilesDoesNotScan()
    {
        var backend = new Backend(root) { Fail = true };
        Assert.AreEqual(
            64, (await Run(Args("prepare", "--profile", "unknown", "--yes"), backend)).Code);
        var profiles = await Run(Args("profiles"), backend);
        Assert.AreEqual(0, profiles.Code);
        Assert.AreEqual(5, profiles.Json.RootElement.GetProperty("profiles").GetArrayLength());
        Assert.AreEqual(0, backend.Installed.Count);
        Assert.IsFalse(Directory.Exists(root));
    }

    [TestMethod]
    public async Task DoctorReportsUnhealthyThenDetectsLiveDriftAfterSuccessfulPreparation()
    {
        var backend = new Backend(root);
        Assert.AreEqual(2,
                        (await Run(Args("doctor", "--profile", "python-pipeline"), backend)).Code);
        Assert.AreEqual(0, backend.Installed.Count);
        Assert.AreEqual(
            0, (await Run(Args("prepare", "--profile", "python-pipeline", "--yes"), backend)).Code);
        CollectionAssert.AreEqual(new[] { "python.interpreter", "python.build", "python.tools" },
                                  backend.Installed);
        Assert.AreEqual(0,
                        (await Run(Args("doctor", "--profile", "python-pipeline"), backend)).Code);
        backend.Ready.Remove("python.build");
        var drift = await Run(Args("doctor", "--profile", "python-pipeline"), backend);
        Assert.AreEqual(2, drift.Code);
        Assert.AreEqual(
            "Missing",
            drift.Json.RootElement.GetProperty("health").GetProperty("state").GetString());
    }

    [TestMethod]
    public async Task SelectedClosureIsIdempotentAndEvidencePathsAreRedacted()
    {
        var backend = new Backend(root);
        var first = await Run(Args("prepare", "--profile", "managed-tooling", "--profile",
                                   "managed-tooling", "--yes"),
                              backend);
        Assert.AreEqual(0, first.Code);
        CollectionAssert.AreEqual(new[] { "managed.sdk", "managed.packages" }, backend.Installed);
        Assert.IsFalse(first.Output.Contains("private", StringComparison.Ordinal));
        var location = first.Json.RootElement.GetProperty("health")
                           .GetProperty("profiles") [0]
                           .GetProperty("requirements") [0]
                           .GetProperty("evidence") [0]
                           .GetProperty("location");
        Assert.AreEqual("redacted", location.GetProperty("kind").GetString());
        Assert.AreEqual(JsonValueKind.Null, location.GetProperty("value").ValueKind);
        await Run(Args("prepare", "--profile", "managed-tooling", "--yes"), backend);
        Assert.AreEqual(2, backend.Installed.Count);
        foreach (var line in first.Error.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            using var record = JsonDocument.Parse(line);
            Assert.IsTrue(record.RootElement.TryGetProperty("message", out _));
        }
    }

    [TestMethod]
    public async Task FailedPreparationAndPendingActionsHaveDistinctExitCodes()
    {
        var failed = new Backend(root) { Outcome = RecipeExecutionStatus.Failed };
        Assert.AreEqual(
            3, (await Run(Args("prepare", "--profile", "python-pipeline", "--yes"), failed)).Code);
        failed.Outcome = RecipeExecutionStatus.RestartRequired;
        var pending = await Run(Args("prepare", "--profile", "python-pipeline", "--yes"), failed);
        Assert.AreEqual(2, pending.Code);
        Assert.IsTrue(pending.Output.Contains("RestartRequired", StringComparison.Ordinal));
        var acknowledged = await Run(
            Args("doctor", "--profile", "python-pipeline", "--acknowledge-actions"), failed);
        Assert.AreEqual(2, acknowledged.Code);
        Assert.IsFalse(acknowledged.Output.Contains("RestartRequired", StringComparison.Ordinal));
    }

    [TestMethod]
    public async Task BackendFailureIsSanitizedAndDoesNotShowAnyDialog()
    {
        var result = await Run(Args("doctor", "--profile", "python-pipeline"),
                               new Backend(root) { Fail = true });
        Assert.AreEqual(70, result.Code);
        Assert.IsFalse(
            (result.Output + result.Error).Contains("do-not-print", StringComparison.Ordinal));
    }

    [TestMethod]
    public async Task DeadlineAndCallerCancellationProduceStructuredResults()
    {
        var backend = new Backend(root) { Wait = true };
        Assert.AreEqual(130, (await Run(Args("doctor", "--profile", "python-pipeline",
                                             "--timeout-seconds", "1"),
                                        backend))
                                 .Code);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Assert.AreEqual(130, (await Run(Args("doctor", "--profile", "python-pipeline"), backend,
                                        cancellation.Token))
                                 .Code);
    }

    [TestMethod]
    public async Task PublishedHeadlessProcessRunsWithNoDeveloperRuntimesOrUi()
    {
        var executable = System.Environment.GetEnvironmentVariable("RAIDERS_SETUP_PUBLISHED_EXE");
        if (executable is null)
        {
            Assert.Inconclusive("Set RAIDERS_SETUP_PUBLISHED_EXE to validate the published " +
                                "headless entry point (required in CI).");
        }

        Directory.CreateDirectory(root);
        foreach (var command in new[] { "--help", "profiles", "doctor", "invalid" })
        {
            var start = new ProcessStartInfo(executable) {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true,
                RedirectStandardError = true, WorkingDirectory = root
            };
            start.ArgumentList.Add(command);
            if (command is "profiles" or "doctor")
            {
                start.ArgumentList.Add("--state-root");
                start.ArgumentList.Add(root);
            }

            if (command == "doctor")
            {
                start.ArgumentList.Add("--profile");
                start.ArgumentList.Add("lua-development");
            }

            start.Environment["PATH"] = "";
            start.Environment["DOTNET_ROOT"] = Path.Combine(root, "absent-dotnet");
            start.Environment["DOTNET_ROOT_X64"] = start.Environment["DOTNET_ROOT"];
            start.Environment["PYTHONHOME"] = Path.Combine(root, "absent-python");
            using var process = Process.Start(start)!;
            try
            {
                var output = process.StandardOutput.ReadToEndAsync();
                var error = process.StandardError.ReadToEndAsync();
                await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(20));
                var text = await output;
                using var result = JsonDocument.Parse(text);
                Assert.AreEqual(command == "invalid"  ? 64
                                : command == "doctor" ? 2
                                                      : 0,
                                process.ExitCode);
                Assert.AreEqual(process.ExitCode,
                                result.RootElement.GetProperty("exit_code").GetInt32());
                Assert.AreEqual(IntPtr.Zero, process.MainWindowHandle);
                Assert.IsFalse(
                    text.Contains("Headless command finished", StringComparison.Ordinal));
                Assert.IsTrue(
                    (await error).Contains("Headless command finished", StringComparison.Ordinal));
            }
            finally
            {
                if (!process.HasExited)
                {
                    process.Kill(entireProcessTree: true);
                    await process.WaitForExitAsync();
                }
            }
        }
    }
}
