using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Raiders.Diagnostics;

[assembly:Parallelize(Scope = ExecutionScope.MethodLevel)]

namespace Raiders.Environment.Core.Tests;

[TestClass]
public sealed class ProbeTests
{
    private static readonly MachineIdentity Host = new("windows", "x64");
    private static readonly string Hash = new('a', 64);
    private readonly string temporary =
        Path.Combine(Path.GetTempPath(), "raiders-probes-" + Guid.NewGuid().ToString("N"));

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

    private static JsonObject CatalogNode()
    {
        return JsonNode.Parse(Catalog())!.AsObject();
    }

    private static DiscoveryResult Satisfied(ToolRequirement requirement)
    {
        var version =
            requirement.Constraint.Value ?? requirement.Constraint.Values?.FirstOrDefault();
        return new("found",
                   [new(version, requirement.Architecture, Hash, "C:/private/user/tool.exe")],
                   Hash);
    }

    private static EnvironmentProbe Probe(IToolDiscovery discovery, StringWriter? sink = null)
    {
        var telemetry = new TelemetrySession("environment.test");
        return new(discovery, telemetry, sink is null ? null : new Log(telemetry, sink));
    }

    private static ToolDiscovery Registry(
        Func<ToolRequirement, DiscoveryResult>? observation = null)
    {
        var registry = new ToolDiscovery();
        foreach (var requirement in EnvironmentProbe.ReadCatalog(Catalog())
                     .Profiles.SelectMany(profile => profile.Requirements)
                     .DistinctBy(item => item.ProbeId))
        {
            registry.Register(requirement.ProbeId, (item, token) => ValueTask.FromResult(
                                                       (observation ?? Satisfied)(item)));
        }

        return registry;
    }

    [TestMethod]
    public async Task CompleteSelectedScanSerializesContractWithoutPrivatePaths()
    {
        using var log = new StringWriter();
        var report =
            await Probe(Registry(), log).ScanAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual("Ready", report.State);
        CollectionAssert.AreEqual(new[] { "python-pipeline" }, report.ResolvedProfiles);
        Assert.AreEqual(3, report.Profiles.Single().Requirements.Length);
        Assert.IsTrue(report.ScanComplete);
        Assert.EndsWith("Z", report.StartedAt);
        Assert.EndsWith("Z", report.CompletedAt);
        using var json = JsonDocument.Parse(report.ToJson());
        Assert.AreEqual(1, json.RootElement.GetProperty("schema_version").GetInt32());
        Assert.AreEqual("shareable", json.RootElement.GetProperty("visibility").GetString());
        Assert.DoesNotContain("private", report.ToJson());
        Assert.DoesNotContain("private", log.ToString());
        Assert.Contains("probe_id", log.ToString());
        var output = System.Environment.GetEnvironmentVariable("RAIDERS_HEALTH_OUTPUT");
        if (output is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
            await File.WriteAllTextAsync(output, report.ToJson());
        }
    }

    [TestMethod]
    [DataRow("absent", "Missing", "NotFound")]
    [DataRow("failed", "Unknown", "ProbeFailed")]
    [DataRow("timed-out", "Unknown", "ProbeTimedOut")]
    [DataRow("unresolved", "Unknown", "RecipeUnavailable")]
    [DataRow("invalid", "Unknown", "ProbeFailed")]
    public async Task InconclusiveSearchNeverClaimsAbsence(string status, string state,
                                                           string reason)
    {
        var report = await Probe(Registry(
                                     _ => new(status, [])))
                         .ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(state, report.State);
        CollectionAssert.Contains(report.ReasonCodes, reason);
        Assert.IsTrue(
            report.Profiles.All(profile => profile.Requirements.All(
                                    item => item.Evidence[0].Complete == (status == "absent"))));
    }

    [TestMethod]
    [DataRow("10.0.402", "Ready")]
    [DataRow("10.0.450", "Ready")]
    [DataRow("10.0.401", "Needs repair")]
    [DataRow("10.0.500", "Needs repair")]
    [DataRow("11.0.402", "Needs repair")]
    [DataRow("10.0.402-preview.1", "Needs repair")]
    public async Task DotnetFeatureBandMatchesGlobalJsonPolicy(string version, string state)
    {
        var registry =
            Registry(item => item.ProbeId == "managed.sdk" ? new("found", [new(version, "x64")])
                                                           : Satisfied(item));
        var report = await Probe(registry).ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual(state, report.State);
    }

    [TestMethod]
    public async Task CompatibleSdkWinsOverAnOlderInstalledCandidate()
    {
        var registry =
            Registry(item => item.ProbeId == "managed.sdk"
                                 ? new("found", [new("9.0.100", "x64"), new("10.0.402", "x64")])
                                 : Satisfied(item));
        var report = await Probe(registry).ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual("Ready", report.State);
    }

    [TestMethod]
    [DataRow("3.12.10", "x64", "Ready", "")]
    [DataRow("3.12.14", "x64", "Ready", "")]
    [DataRow("3.12.15", "x64", "Needs repair", "VersionMismatch")]
    [DataRow("3.12.10", "x86", "Needs repair", "ArchitectureMismatch")]
    [DataRow("3.12.10", "unknown", "Unknown", "MissingEvidence")]
    [DataRow("user-secret-value", "x64", "Unknown", "MissingEvidence")]
    public async Task PythonPinsArchitectureAndSanitizationAreEnforced(string version,
                                                                       string architecture,
                                                                       string state, string reason)
    {
        var registry = Registry(item => item.Kind == "python-interpreter"
                                            ? new("found", [new(version, architecture)])
                                            : Satisfied(item));
        var report = await Probe(registry).ScanAsync(Catalog(), ["python-pipeline"], machine: Host);
        Assert.AreEqual(state, report.State);
        if (reason.Length > 0)
        {
            CollectionAssert.Contains(report.ReasonCodes, reason);
        }

        Assert.DoesNotContain("user-secret", report.ToJson());
    }

    [TestMethod]
    public async Task OptionalDesignerFailureDoesNotBlockReadyOrScanDeveloperProfiles()
    {
        var checkedIds = new List<string>();
        var registry = Registry(item =>
                                {
                                    checkedIds.Add(item.Id);
                                    return item.Required ? Satisfied(item) : new("failed", []);
                                });
        var report = await Probe(registry).ScanAsync(Catalog(), ["designer-tools"], machine: Host);
        Assert.AreEqual("Ready", report.State);
        Assert.AreEqual("Unknown",
                        report.Profiles[0].Requirements.Single(item => !item.Required).State);

        Assert.AreEqual(2, checkedIds.Count);
    }

    [TestMethod]
    public async Task ClosureAndDependencyStatesAreComputedWithoutScanningUnrelatedProfiles()
    {
        var node = CatalogNode();
        node["profiles"]![1]!["depends_on"] = new JsonArray("native-development");
        var registry =
            Registry(item => item.Id == "native.msvc" ? new("absent", []) : Satisfied(item));
        var report = await Probe(registry).ScanAsync(node.ToJsonString(), ["managed-tooling"],
                                                     machine: Host);
        CollectionAssert.AreEqual(new[] { "native-development", "managed-tooling" },
                                  report.ResolvedProfiles);
        var managed = report.Profiles[1];
        Assert.AreEqual("Missing", managed.State);
        Assert.AreEqual("Missing", managed.Dependencies.Single().State);
        CollectionAssert.AreEqual(new[] { "DependencyNotReady" }, managed.ReasonCodes);
        Assert.IsTrue(managed.Requirements.All(item => item.State == "Ready"));
    }

    [TestMethod]
    [DataRow("other", "x64", "Unsupported", "UnsupportedPlatform")]
    [DataRow("windows", "arm64", "Unsupported", "UnsupportedArchitecture")]
    [DataRow("windows", "x86", "Unsupported", "UnsupportedArchitecture")]
    [DataRow("windows", "unknown", "Unknown", "MissingEvidence")]
    public async Task UnsupportedOrUnidentifiedHostDoesNotExecuteTools(string operatingSystem,
                                                                       string architecture,
                                                                       string state, string reason)
    {
        var report = await Probe(Registry(
                                     _ => throw new AssertFailedException("Probe must not run.")))
                         .ScanAsync(Catalog(), ["native-development"],
                                    machine: new(operatingSystem, architecture));
        Assert.AreEqual(state, report.State);
        CollectionAssert.Contains(report.ReasonCodes, reason);
    }

    [TestMethod]
    public async Task ConfirmedRepairOutranksMissingAndFailedProbes()
    {
        var registry =
            Registry(item => item.Id switch { "native.msvc" => new("failed", []),
                                              "native.toolset" => new("absent", []),
                                              "native.cmake" => new("found", [new("0.0.1", "x64")]),
                                              _ => Satisfied(item) });
        var report =
            await Probe(registry).ScanAsync(Catalog(), ["native-development"], machine: Host);
        Assert.AreEqual("Needs repair", report.State);
        CollectionAssert.AreEquivalent(new[] { "ProbeFailed", "NotFound", "VersionMismatch" },
                                       report.ReasonCodes);
    }

    [TestMethod]
    public async Task RecheckingNeverTrustsPreviousReadyResult()
    {
        var stale = false;
        var registry =
            Registry(item => item.Kind == "package-feed" && stale
                                 ? new("found", [new(null, "any", new string('b', 64))], Hash)
                                 : Satisfied(item));
        var probe = Probe(registry);
        var first =
            await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host, shareable: false);
        stale = true;
        var second = await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual("Ready", first.State);
        Assert.Contains("private", first.ToJson());
        Assert.AreEqual("Needs repair", second.State);
        CollectionAssert.Contains(second.ReasonCodes, "FingerprintMismatch");
        Assert.AreNotEqual(first.ScanId, second.ScanId);
    }

    [TestMethod]
    public async Task FailedAndTimedOutAdaptersAreSanitizedAndCancellationPropagates()
    {
        var registry = new ToolDiscovery();
        registry.Register("managed.sdk", (_, _) => throw new TimeoutException("private-secret"));

        registry.Register("managed.packages", (_, _) => throw new IOException("private-secret"));

        var report = await Probe(registry).ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        CollectionAssert.AreEquivalent(new[] { "ProbeTimedOut", "ProbeFailed" },
                                       report.ReasonCodes);
        Assert.DoesNotContain("private-secret", report.ToJson());
        using var source = new CancellationTokenSource();
        source.Cancel();
        await Assert.ThrowsAsync<OperationCanceledException>(
            () => Probe(registry).ScanAsync(Catalog(), ["managed-tooling"], machine: Host,
                                            cancellationToken: source.Token));
    }

    [TestMethod]
    [DataRow("duplicate")]
    [DataRow("unknown")]
    [DataRow("cycle")]
    [DataRow("requirement-cycle")]
    [DataRow("required-optional")]
    [DataRow("designer-sdk")]
    [DataRow("schema")]
    [DataRow("constraint")]
    public void InvalidCatalogFailsBeforeAnyProbe(string mutation)
    {
        var node = CatalogNode();
        var profiles = node["profiles"]!.AsArray();
        var requirements = profiles[2]!["requirements"]!.AsArray();
        switch (mutation)
        {
        case "duplicate":
            profiles.Add(profiles[0]!.DeepClone());
            break;
        case "unknown":
            profiles[0]!["depends_on"] = new JsonArray("nonexistent");
            break;
        case "cycle":
            profiles[0]!["depends_on"] = new JsonArray("native-development");
            break;
        case "requirement-cycle":
            requirements[0]!["depends_on"] = new JsonArray("python.tools");
            break;
        case "required-optional":
            requirements[0]!["required"] = false;
            break;
        case "designer-sdk":
            profiles[4]!["depends_on"] = new JsonArray("managed-tooling");
            break;
        case "schema":
            node["schema_version"] = 2;
            break;
        case "constraint":
            requirements[0]!["constraint"]!["kind"] = "arbitrary";
            break;
        }

        Assert.Throws<ArgumentException>(() => EnvironmentProbe.ReadCatalog(node.ToJsonString()));
    }

    [TestMethod]
    public void CatalogFingerprintMatchesY01NewlineAndBomRules()
    {
        var catalog = Catalog().Replace("\r\n", "\n");
        Assert.AreEqual(
            EnvironmentProbe.CatalogFingerprint(catalog),
            EnvironmentProbe.CatalogFingerprint("\uFEFF" + catalog.Replace("\n", "\r\n")));
        Assert.AreNotEqual(EnvironmentProbe.CatalogFingerprint(catalog),
                           EnvironmentProbe.CatalogFingerprint(catalog + " "));
    }

    private string FakeExecutable()
    {
        Directory.CreateDirectory(temporary);
        var metadata = new MetadataBuilder();
        metadata.AddModule(0, metadata.GetOrAddString("fixture"),
                           metadata.GetOrAddGuid(Guid.NewGuid()), default, default);
        var pe = new ManagedPEBuilder(new PEHeaderBuilder(machine: Machine.Amd64),
                                      new MetadataRootBuilder(metadata), new BlobBuilder());
        var buffer = new BlobBuilder();
        pe.Serialize(buffer);
        var path = Path.Combine(temporary, "fixture.exe");
        File.WriteAllBytes(path, buffer.ToArray());
        return path;
    }

    [TestMethod]
    [DataRow(ToolProtocol.Msvc, "native.msvc", "",
             "Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35229.0 for x64", 2)]
    [DataRow(ToolProtocol.CMake, "native.cmake", "cmake version 3.31.6-msvc6", "", 0)]
    [DataRow(ToolProtocol.Ninja, "native.ninja", "1.12.1", "", 0)]
    [DataRow(ToolProtocol.DotnetSdk, "managed.sdk", "9.0.100 [C:/sdk]\n10.0.402 [C:/sdk]", "", 0)]
    [DataRow(ToolProtocol.Python, "python.interpreter", "{\"version\":\"3.12.14\",\"bits\":64}", "",
             0)]
    [DataRow(ToolProtocol.LuaJit, "lua.runtime", "", "LuaJIT 2.1.0-beta3 -- Copyright", 0)]
    public async Task ExecutableAdaptersParseVersionAndVerifyPeArchitecture(
        ToolProtocol protocol, string id, string output, string error, int exitCode)
    {
        ProbeCommand? command = null;
        var registry = new ToolDiscovery((request, _) =>
                                         {
                                             command = request;
                                             return Task.FromResult(
                                                 new CommandResult(exitCode, output, error));
                                         });
        registry.RegisterExecutable(id, protocol, FakeExecutable());
        var requirement = EnvironmentProbe.ReadCatalog(Catalog())
                              .Profiles.SelectMany(profile => profile.Requirements)
                              .Single(item => item.Id == id);
        var result = await registry.DiscoverAsync(requirement, default);
        Assert.AreEqual("found", result.Status);
        Assert.IsTrue(result.Candidates.All(candidate => candidate.Architecture == "x64"));

        Assert.IsNotNull(command);
        if (protocol == ToolProtocol.Python)
        {
            Assert.AreEqual("-I", command.Arguments[0]);
        }
    }

    [TestMethod]
    public async Task MalformedExecutableOutputCannotProveMissingAndEmptySdkListCan()
    {
        var requirement = EnvironmentProbe.ReadCatalog(Catalog()).Profiles[1].Requirements[0];
        var output = "not a version";
        var registry =
            new ToolDiscovery((_, _) => Task.FromResult(new CommandResult(0, output, "")));
        registry.RegisterExecutable(requirement.ProbeId, ToolProtocol.DotnetSdk, FakeExecutable());
        Assert.AreEqual("failed", (await registry.DiscoverAsync(requirement, default)).Status);
        output = "";
        Assert.AreEqual("absent", (await registry.DiscoverAsync(requirement, default)).Status);
    }

    [TestMethod]
    public async Task ManifestProbesRehashPackageClosureInsteadOfTrustingReceipt()
    {
        Directory.CreateDirectory(temporary);
        var file = Path.Combine(temporary, "NuGet.config");
        await File.WriteAllTextAsync(file, "approved configuration");
        var hashes =
            new Dictionary<string, string> { ["NuGet.config"] =
                                                 Convert.ToHexStringLower(SHA256.HashData(
                                                     await File.ReadAllBytesAsync(file))) };
        var packageRegistry = new ToolDiscovery();
        packageRegistry.RegisterManifest("managed.packages", "package-feed", "nuget-config-v1",
                                         temporary, null, "any", hashes);
        var registry = new ToolDiscovery();
        registry.Register("managed.sdk", (item, _) => ValueTask.FromResult(Satisfied(item)));

        registry.Register("managed.packages", packageRegistry.DiscoverAsync);
        var probe = Probe(registry);
        Assert.AreEqual(
            "Ready", (await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host)).State);
        await File.WriteAllTextAsync(file, "changed configuration");
        var stale = await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        Assert.AreEqual("Needs repair", stale.State);
        CollectionAssert.Contains(stale.ReasonCodes, "StateStale");
        File.Delete(file);
        var partial = await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        CollectionAssert.Contains(partial.ReasonCodes, "IncompleteSetup");
    }

    [TestMethod]
    [DataRow("Ready", 0, "found")]
    [DataRow("Missing", 1, "absent")]
    [DataRow("Needs repair", 1, "found")]
    [DataRow("Ready", 1, "failed")]
    [DataRow("Checking", 0, "failed")]
    public void FreshPythonHealthInputIsMappedWithoutCopyingRawReasons(string state, int exitCode,
                                                                       string status)
    {
        var json = JsonSerializer.Serialize(
            new { profile = "tools", status = state, reasons = new[] { "private" } });
        var result = ToolDiscovery.PythonEnvironmentResult(json, exitCode, "tools", Hash, Hash);
        Assert.AreEqual(status, result.Status);
        Assert.DoesNotContain("private", JsonSerializer.Serialize(result));
    }

    [TestMethod]
    public async Task UnregisteredLuaPolicyRemainsUnknownAndDoesNotInventAPin()
    {
        var report = await Probe(new ToolDiscovery())
                         .ScanAsync(Catalog(), ["lua-development"], machine: Host);
        Assert.AreEqual("Unknown", report.State);
        CollectionAssert.AreEqual(new[] { "RecipeUnavailable" }, report.ReasonCodes);
    }

    [TestMethod]
    public async Task AssessmentProfilingFollowsBuildSwitchAndCarriesScanCorrelation()
    {
        var session = new TelemetrySession("environment.profile");
        var probe = new EnvironmentProbe(Registry(), session);
        var report = await probe.ScanAsync(Catalog(), ["managed-tooling"], machine: Host);
        var events = session.Drain();
        Assert.AreEqual(TelemetrySession.ProfilingEnabled ? 2 : 0, events.Length);
        Assert.IsTrue(events.All(item => item.Context.CorrelationId == report.ScanId));

        Assert.IsNull(session.Context.CorrelationId);
    }

    [TestMethod]
    [DataRow("native.toolset")]
    [DataRow("native.windows-sdk")]
    [DataRow("lua.runtime")]
    public async Task ApprovedSdkToolsetAndLuaManifestRequireCurrentFiles(string id)
    {
        var requirement = EnvironmentProbe.ReadCatalog(Catalog())
                              .Profiles.SelectMany(profile => profile.Requirements)
                              .Single(item => item.Id == id);
        var registry = new ToolDiscovery();
        var path = Path.Combine(temporary, "artifact.bin");
        var bytes = new byte[] { 1, 2, 3, 4 };
        registry.RegisterManifest(
            requirement.ProbeId, requirement.Kind, requirement.Constraint.Reference, temporary,
            requirement.Constraint.Value ?? "2.1.0-beta3", "x64",
            new Dictionary<string, string> { ["artifact.bin"] = Convert.ToHexStringLower(
                                                 SHA256.HashData(bytes)) });
        Assert.AreEqual("absent", (await registry.DiscoverAsync(requirement, default)).Status);
        Directory.CreateDirectory(temporary);
        await File.WriteAllBytesAsync(path, bytes);
        var found = await registry.DiscoverAsync(requirement, default);
        Assert.AreEqual("found", found.Status);
        Assert.AreEqual(found.ExpectedFingerprint, found.Candidates[0].Fingerprint);
        await File.WriteAllBytesAsync(path, [5]);
        Assert.AreEqual("StateStale",
                        (await registry.DiscoverAsync(requirement, default)).Candidates[0].Problem);
    }

    [TestMethod]
    [DataRow("../outside")]
    [DataRow("/root")]
    [DataRow("C:/root")]
    [DataRow("folder\\file")]
    public void ManifestRejectsPathsOutsideItsOwnedRoot(string path)
    {
        Assert.Throws<ArgumentException>(() => new ToolDiscovery().RegisterManifest(
                                             "probe", "artifact-bundle", "approved", temporary,
                                             "1.0.0", "x64",
                                             new Dictionary<string, string> { [path] = Hash }));
    }

    [TestMethod]
    public void DuplicateJsonMembersAndUnknownFieldsAreRejected()
    {
        Assert.Throws<ArgumentException>(
            () => EnvironmentProbe.ReadCatalog(Catalog().Replace(
                "\"schema_version\": 1", "\"schema_version\": 2, \"schema_version\": 1")));

        var node = CatalogNode();
        node["command"] = "never execute catalog strings";
        Assert.Throws<JsonException>(() => EnvironmentProbe.ReadCatalog(node.ToJsonString()));
    }
}
