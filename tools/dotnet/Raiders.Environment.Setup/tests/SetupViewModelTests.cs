using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Raiders.Diagnostics;
using Raiders.Environment.Core;

[assembly:DoNotParallelize]

namespace Raiders.Environment.Setup.Tests;

[TestClass]
public sealed class SetupViewModelTests
{
    private readonly string temporary =
        Path.Combine(Path.GetTempPath(), "raiders-setup-ui-" + Guid.NewGuid().ToString("N"));

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(temporary))
        {
            Directory.Delete(temporary, recursive: true);
        }
    }

    private string Catalog()
    {
        return new WindowsSetupBackend(temporary, new TelemetrySession("setup.policy.test"))
            .CatalogJson;
    }

    [TestMethod]
    public void EmbeddedRequirementsMatchReviewedCatalogContract()
    {
        var json = Catalog();
        var catalog = EnvironmentProbe.ReadCatalog(json);
        var expected = EnvironmentProbe.ReadCatalog(
            File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "environment-profile.json")));
        CollectionAssert.AreEquivalent(expected.Profiles.Select(profile => profile.Id).ToArray(),
                                       catalog.Profiles.Select(profile => profile.Id).ToArray());

        foreach (var profile in expected.Profiles)
        {
            Assert.AreEqual(
                JsonSerializer.Serialize(profile.Requirements),
                JsonSerializer.Serialize(
                    catalog.Profiles.Single(item => item.Id == profile.Id).Requirements));
        }

        var output = System.Environment.GetEnvironmentVariable("RAIDERS_SETUP_CATALOG_OUTPUT");
        if (output is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
            File.WriteAllText(output, json);
        }
    }

    [TestMethod]
    public void StartupHasEmbeddedPolicyAndDoesNotCreateEnvironmentOrRequireTools()
    {
        using var model = new ProfileViewModel(new FakeBackend(Catalog(), temporary));
        Assert.AreEqual(5, model.Catalog.Profiles.Length);
        Assert.AreEqual(0, model.Selected.Length);
        Assert.IsFalse(model.CanScan);
        Assert.IsFalse(model.CanPrepare);
        Assert.IsFalse(Directory.Exists(temporary));
        Assert.IsFalse(model.Catalog.SetupHost.RequiresInstalledSdk);
        Assert.IsFalse(model.Catalog.SetupHost.RequiresPython);
    }

    [TestMethod]
    public async Task SelectionRequiresFreshScanAndOnlySelectedClosureIsPrepared()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("designer-tools", true);
        Assert.IsTrue(model.CanScan);
        Assert.IsFalse(model.CanPrepare);
        await model.ScanAsync();
        Assert.IsTrue(model.CanPrepare);
        Assert.AreEqual("Missing", model.ProfileState("designer-tools"));
        Assert.IsTrue(model.Actions.Any(action => action.Action == "Optional"));

        await model.PrepareAsync();
        Assert.AreEqual("Ready", model.ProfileState("designer-tools"));
        CollectionAssert.AreEqual(new[] { "designer.sdk" }, backend.Installed);
        Assert.IsFalse(model.CanPrepare);
        model.Select("managed-tooling", true);
        Assert.IsNull(model.Health);
        Assert.IsFalse(model.CanPrepare);
    }

    [TestMethod]
    public async Task ReadyEnvironmentCannotBePreparedAgain()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("managed-tooling", true);
        await model.ScanAsync();
        await model.PrepareAsync();
        await model.ScanAsync();
        Assert.IsFalse(model.CanPrepare);
        Assert.IsTrue(model.Actions.All(action => action.Action == "Up to date"));

        await Assert.ThrowsAsync<InvalidOperationException>(model.PrepareAsync);
        Assert.AreEqual(2, backend.Installed.Count);
    }

    [TestMethod]
    public async Task UnavailableRecipesAreVisibleAndNeverOfferedAsInstallActions()
    {
        var backend = new FakeBackend(Catalog(), temporary, available: false);
        using var model = new ProfileViewModel(backend);
        model.Select("lua-development", true);
        await model.ScanAsync();
        Assert.IsTrue(model.Actions.All(action => action.Action == "Unavailable"));

        Assert.IsFalse(model.CanPrepare);
    }

    [TestMethod]
    public async Task BackgroundWorkKeepsSelectionLockedAndCanBeCancelled()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("python-pipeline", true);
        backend.WaitOnScan = true;
        var scan = model.ScanAsync();
        await backend.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.IsTrue(model.IsBusy);
        Assert.IsFalse(model.CanScan);
        Assert.IsFalse(model.CanPrepare);
        Assert.Throws<InvalidOperationException>(() => model.Select("designer-tools", true));

        model.Cancel();
        await scan;
        Assert.IsFalse(model.IsBusy);
        Assert.IsNull(model.Health);
        Assert.Contains("cancelled", model.Message);
        backend.WaitOnScan = false;
        await model.ScanAsync();
        Assert.IsNotNull(model.Health);
    }

    [TestMethod]
    public async Task PreparationCancellationInvalidatesOldHealthAndRestoresCommands()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("python-pipeline", true);
        await model.ScanAsync();
        backend.WaitOnPrepare = true;
        var preparation = model.PrepareAsync();
        await backend.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        model.Cancel();
        await preparation;
        Assert.IsNull(model.Health);
        Assert.IsTrue(model.CanScan);
        Assert.IsFalse(model.CanPrepare);
        Assert.AreEqual(0, backend.Installed.Count);
    }

    [TestMethod]
    public async Task FailureIsReportedWithoutRawExceptionText()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("managed-tooling", true);
        backend.FailScan = true;
        await model.ScanAsync();
        Assert.Contains("failed", model.Message);
        Assert.DoesNotContain("private", model.Message);
        Assert.IsNull(model.Health);
        Assert.IsFalse(model.IsBusy);
        backend.FailScan = false;
        await model.ScanAsync();
        backend.Outcome = RecipeExecutionStatus.Failed;
        await model.PrepareAsync();
        Assert.AreEqual(PreparationStatus.Failed, model.Preparation!.Status);
        Assert.Contains("failed", model.Message);
    }

    [TestMethod]
    [DataRow(RecipeExecutionStatus.RestartRequired)]
    [DataRow(RecipeExecutionStatus.PrivilegeRequired)]
    public async Task
    PendingActionsSurviveRescanAndProfileSelectionChanges(RecipeExecutionStatus outcome)
    {
        var backend = new FakeBackend(Catalog(), temporary);
        using var model = new ProfileViewModel(backend);
        model.Select("managed-tooling", true);
        await model.ScanAsync();
        backend.Outcome = outcome;
        await model.PrepareAsync();
        Assert.IsTrue(model.HasPendingAction);
        Assert.IsFalse(model.CanPrepare);
        await model.ScanAsync();
        Assert.AreEqual("Needs repair", model.ProfileState("managed-tooling"));
        Assert.IsTrue(model.Actions.Any(action => action.Action == "Action required"));

        model.Select("managed-tooling", false);
        model.Select("designer-tools", true);
        backend.Outcome = RecipeExecutionStatus.Completed;
        await model.ScanAsync();
        await model.PrepareAsync();
        model.Select("designer-tools", false);
        model.Select("managed-tooling", true);
        await model.ScanAsync();
        Assert.IsTrue(model.HasPendingAction);
        Assert.AreEqual("Needs repair", model.ProfileState("managed-tooling"));
    }

    [TestMethod]
    public async Task RealWindowBindsControlsEvidenceAndProgressOnUiThread()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        await InWindow(
            backend,
            async (window, model) =>
            {
                var profiles = (ListView)window.Controls.Find("Profiles", true).Single();
                profiles.Items[2].Checked = true;
                Assert.IsTrue(window.Controls.Find("Scan", true).Single().Enabled);
                Assert.IsFalse(window.Controls.Find("Prepare", true).Single().Enabled);
                backend.Ready.Add("python.interpreter");
                await model.ScanAsync();
                var grid = (DataGridView)window.Controls.Find("Checks", true).Single();
                Assert.AreEqual(3, grid.Rows.Count);
                grid.CurrentCell = grid.Rows[0].Cells[0];
                var evidence = window.Controls.Find("Evidence", true).Single();
                Assert.Contains("3.12.", evidence.Text);
                Assert.Contains("x64", evidence.Text);
                Assert.IsTrue(window.Controls.Find("Prepare", true).Single().Enabled);
                Assert.IsFalse(window.Controls.Find("Cancel", true).Single().Enabled);
                var output = System.Environment.GetEnvironmentVariable("RAIDERS_SETUP_PREVIEW");
                if (output is not null)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
                    using var bitmap = new Bitmap(window.Width, window.Height);
                    window.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height));
                    bitmap.Save(output, ImageFormat.Png);
                }

                await model.PrepareAsync();
                Assert.AreEqual("Ready", model.ProfileState("python-pipeline"));
                Assert.IsFalse(window.Controls.Find("Prepare", true).Single().Enabled);
            });
    }

    [TestMethod]
    public async Task WindowShowsLiveProgressAndCancellationWithoutBlockingTheUi()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        await InWindow(
            backend,
            async (window, model) =>
            {
                var profiles = (ListView)window.Controls.Find("Profiles", true).Single();
                profiles.Items[2].Checked = true;
                await model.ScanAsync();
                backend.WaitOnPrepare = true;
                var work = model.PrepareAsync();
                await backend.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
                while (model.Progress is null)
                {
                    await Task.Delay(10);
                }

                Assert.IsFalse(profiles.Enabled);
                Assert.IsFalse(window.Controls.Find("Scan", true).Single().Enabled);
                Assert.IsTrue(window.Controls.Find("Cancel", true).Single().Enabled);
                Assert.Contains("Executing", window.Controls.Find("Status", true).Single().Text);
                ((Button)window.Controls.Find("Cancel", true).Single()).PerformClick();
                await work;
                Assert.IsTrue(profiles.Enabled);
                Assert.IsFalse(window.Controls.Find("Cancel", true).Single().Enabled);
                Assert.Contains("cancelled", window.Controls.Find("Status", true).Single().Text);
            });
    }

    [TestMethod]
    public async Task ClosingBusyWindowCancelsBeforeDisposingControls()
    {
        var backend = new FakeBackend(Catalog(), temporary);
        await InWindow(backend, async (window, model) =>
                                {
                                    model.Select("managed-tooling", true);
                                    backend.WaitOnScan = true;
                                    var scan = model.ScanAsync();
                                    await backend.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
                                    window.Close();
                                    Assert.IsFalse(window.IsDisposed);
                                    await scan;
                                    Assert.IsFalse(model.IsBusy);
                                });
    }

    private static async Task InWindow(ISetupBackend backend,
                                       Func<SetupWindow, ProfileViewModel, Task> body)
    {
        var completed =
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
                                {
                                    Exception? failure = null;
                                    try
                                    {
                                        Application.EnableVisualStyles();
                                        using var model = new ProfileViewModel(backend);
                                        using var window = new SetupWindow(
                                            model) { ShowInTaskbar = false,
                                                     StartPosition = FormStartPosition.Manual,
                                                     Location = new Point(-20000, -20000) };
                                        window.Shown += async (_, _) =>
                                        {
                                            try
                                            {
                                                await body(window, model);
                                            }
                                            catch (Exception error)
                                            {
                                                failure = error;
                                            }
                                            finally
                                            {
                                                window.Close();
                                            }
                                        };

                                        Application.Run(window);
                                        if (failure is not null)
                                        {
                                            completed.TrySetException(failure);
                                        }
                                        else
                                        {
                                            completed.TrySetResult();
                                        }
                                    }
                                    catch (Exception error)
                                    {
                                        completed.TrySetException(error);
                                    }
                                });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();
        await completed.Task.WaitAsync(TimeSpan.FromSeconds(20));
    }

    [TestMethod]
    public async Task PublishedExecutableLaunchesWithoutDeveloperRuntimes()
    {
        var executable = System.Environment.GetEnvironmentVariable("RAIDERS_SETUP_PUBLISHED_EXE");
        if (executable is null)
        {
            Assert.Inconclusive("Set RAIDERS_SETUP_PUBLISHED_EXE to validate the self-contained " +
                                "publish (required in CI).");
        }

        Directory.CreateDirectory(temporary);
        var trace = Path.Combine(temporary, "host.log");
        var start = new ProcessStartInfo(executable) { UseShellExecute = false,
                                                       WorkingDirectory = temporary,
                                                       WindowStyle = ProcessWindowStyle.Hidden };
        start.Environment["PATH"] = "";
        start.Environment["DOTNET_ROOT"] = Path.Combine(temporary, "no-dotnet");
        start.Environment["DOTNET_ROOT_X64"] = start.Environment["DOTNET_ROOT"];
        start.Environment["DOTNET_MULTILEVEL_LOOKUP"] = "0";
        start.Environment["PYTHONHOME"] = Path.Combine(temporary, "no-python");
        start.Environment["DOTNET_BUNDLE_EXTRACT_BASE_DIR"] = Path.Combine(temporary, "bundle");
        start.Environment["DOTNET_HOST_TRACE"] = "1";
        start.Environment["DOTNET_HOST_TRACEFILE"] = trace;
        using var process = Process.Start(start)!;
        try
        {
            Assert.IsTrue(process.WaitForInputIdle(20000),
                          "Published setup did not reach its UI message loop.");
            var deadline = Stopwatch.StartNew();
            var window = OwnedWindow(process.Id);
            while (window == IntPtr.Zero && deadline.Elapsed < TimeSpan.FromSeconds(10))
            {
                Assert.IsFalse(process.HasExited,
                               "Published setup exited before showing its window.");
                await Task.Delay(50);
                process.Refresh();
                window = OwnedWindow(process.Id);
            }

            Assert.AreNotEqual(IntPtr.Zero, window,
                               "Published setup did not create its environment window.");
            Assert.IsTrue(PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero));
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
            Assert.AreEqual(0, process.ExitCode);
            Assert.IsTrue(File.Exists(trace));
            var hostTrace = await File.ReadAllLinesAsync(trace);
            Assert.IsTrue(hostTrace.Any(line => line.Contains("Executing as a self-contained app",
                                                              StringComparison.Ordinal)),
                          "Published setup did not use its bundled runtime.");
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

    private static IntPtr OwnedWindow(int processId)
    {
        var result = IntPtr.Zero;
        EnumWindows(
            (handle, _) =>
            {
                GetWindowThreadProcessId(handle, out var owner);
                if (owner == processId)
                {
                    var title = new StringBuilder(512);
                    GetWindowText(handle, title, title.Capacity);
                    if (title.ToString().Contains("Environment setup", StringComparison.Ordinal))
                    {
                        result = handle;
                        return false;
                    }
                }

                return true;
            },
            IntPtr.Zero);

        return result;
    }

    private delegate bool WindowCallback(IntPtr handle, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(WindowCallback callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr handle, StringBuilder text, int capacity);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool PostMessage(IntPtr handle, uint message, IntPtr first,
                                           IntPtr second);

    private sealed class FakeBackend : ISetupBackend
    {
        private readonly EnvironmentProbe probe;
        private readonly RecipeRunner runner;
        private readonly bool available;
        public string CatalogJson { get; }
        public HashSet<string> Ready { get; } = [];
        public List<string> Installed { get; } = [];
        public TaskCompletionSource Started {
            get;
        } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public bool WaitOnScan;
        public bool WaitOnPrepare;
        public bool FailScan;
        public RecipeExecutionStatus Outcome = RecipeExecutionStatus.Completed;

        public FakeBackend(string catalogJson, string temporary, bool available = true)
        {
            CatalogJson = catalogJson;
            this.available = available;
            var catalog = EnvironmentProbe.ReadCatalog(catalogJson);
            var discovery = new ToolDiscovery();
            foreach (var requirement in catalog.Profiles.SelectMany(profile => profile.Requirements)
                         .DistinctBy(item => item.ProbeId))
            {
                discovery.Register(
                    requirement.ProbeId,
                    (item, _) => ValueTask.FromResult(
                        Ready.Contains(item.Id)
                            ? new DiscoveryResult("found",
                                                  [new(item.Constraint.Value ??
                                                           item.Constraint.Values?.FirstOrDefault(),
                                                       item.Architecture, new string('a', 64),
                                                       "C:/Fixtures/Python/python.exe")],
                                                  new string('a', 64))
                            : new DiscoveryResult("absent", [])));
            }

            var recipes =
                catalog.Profiles
                    .SelectMany(profile => profile.Requirements.Select(
                                    requirement => new SetupRecipe(
                                        requirement.RecipeRef!, profile.Id, requirement.Id,
                                        SetupRecipe.Fingerprint(requirement), [],
                                        (context, _) =>
                                        {
                                            Installed.Add(context.Requirement.Id);
                                            Ready.Add(context.Requirement.Id);
                                            return Task.FromResult(Outcome);
                                        })))
                    .ToArray();
            var telemetry = new TelemetrySession("setup.fixture");
            probe = new(discovery, telemetry);
            runner = new(discovery, available ? recipes : [],
                         new ArtifactAcquisition(Path.Combine(temporary, "cache")), telemetry);
        }

        public bool HasRecipe(string profileId, ToolRequirement requirement)
        {
            return available;
        }

        public async Task<EnvironmentHealth> ScanAsync(string[] selected, CancellationToken token)
        {
            if (FailScan)
            {
                throw new IOException("private credential-bearing exception");
            }

            if (WaitOnScan)
            {
                Started.TrySetResult();
                await Task.Delay(Timeout.Infinite, token);
            }

            return await probe.ScanAsync(CatalogJson, selected, shareable: false,
                                         machine: new("windows", "x64"), cancellationToken: token);
        }

        public async Task<EnvironmentState> PrepareAsync(string[] selected,
                                                         IProgress<PreparationProgress> progress,
                                                         CancellationToken token)
        {
            if (WaitOnPrepare)
            {
                progress.Report(new("fixture-run", "python-pipeline", "python.interpreter",
                                    PreparationStage.Executing, 0, 3));
                Started.TrySetResult();
                await Task.Delay(Timeout.Infinite, token);
            }

            return await runner.RunAsync(CatalogJson, selected, progress, new("windows", "x64"),
                                         token);
        }
    }
}
