using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Raiders.Build.Tests;

[TestClass]
public sealed class PackageGraphTests
{
    private string root = null!;
    private string feed = null!;

    [TestInitialize]
    public void CreateGraph()
    {
        var repository = new DirectoryInfo(AppContext.BaseDirectory);
        while (repository != null && !File.Exists(Path.Combine(repository.FullName, "global.json")))
        {
            repository = repository.Parent;
        }

        Assert.IsNotNull(repository);
        root = Path.Combine(repository.FullName, "out", "tests", "package-graph",
                            Guid.NewGuid().ToString("N"));
        feed = Path.Combine(root, "feed");
        Directory.CreateDirectory(feed);
        string cache = Path.Combine(root, "cache");
        Write("NuGet.config", $@"<configuration>
    <packageSources><clear /><add key=""fixture"" value=""{feed}"" /></packageSources>
    <packageSourceMapping><clear /><packageSource key=""fixture""><package pattern=""*"" /></packageSource></packageSourceMapping>
    <config><add key=""globalPackagesFolder"" value=""{cache}"" /></config>
</configuration>");
        PinVersions("1.0.0");
        Write("Foundation/Foundation.csproj", Library("Foundation"));
        Write("Foundation/Value.cs", "namespace Fixture; public static class Value { public " +
                                         "static string Read() => \"one\"; }");
        Write("Feature/Feature.csproj", Library("Feature", "Foundation"));
        Write("Feature/Feature.cs", "namespace Fixture; public static class Feature { public " +
                                        "static string Read() => Value.Read(); }");
        Write("Consumer/Consumer.csproj", @"<Project Sdk=""Microsoft.NET.Sdk"">
    <PropertyGroup><OutputType>Exe</OutputType><IsPackable>false</IsPackable></PropertyGroup>
    <ItemGroup>
        <PackageReference Include=""Raiders.Fixture.Feature"" />
        <RaidersPackageProject Include=""../Feature/Feature.csproj"" />
    </ItemGroup>
</Project>");
        Write("Consumer/Program.cs", "System.Console.WriteLine(Fixture.Feature.Read());");
        Write("Unrelated/Unrelated.csproj", Library("Unrelated"));
        Write("Unrelated/Broken.cs", "This project must never be built.");
    }

    [TestMethod]
    public void SelectedGraphPacksTransitiveDependenciesBeforeConsumerRestore()
    {
        var result = Restore();
        Success(result);
        string output = result.Output.Replace('/', Path.DirectorySeparatorChar);
        int foundation = output.IndexOf("Local package ready: " + Package("Foundation"),
                                        StringComparison.Ordinal);
        int feature =
            output.IndexOf("Local package ready: " + Package("Feature"), StringComparison.Ordinal);
        Assert.IsTrue(foundation >= 0 && feature > foundation, output);
        Assert.IsTrue(File.Exists(Package("Foundation")));
        Assert.IsTrue(File.Exists(Package("Feature")));
        Assert.IsFalse(File.Exists(Package("Unrelated")));
        using var archive = ZipFile.OpenRead(Package("Feature"));
        Assert.IsFalse(archive.Entries.Any(
            entry => entry.FullName.EndsWith("Foundation.dll", StringComparison.Ordinal)));

        var nuspec = archive.Entries.Single(
            entry => entry.FullName.EndsWith(".nuspec", StringComparison.Ordinal));
        using var reader = new StreamReader(nuspec.Open());
        StringAssert.Contains(reader.ReadToEnd(), "Raiders.Fixture.Foundation");
        AssertLockVersion("Foundation", "Raiders.Fixture.Foundation", "1.0.0", expected: false);
        AssertLockVersion("Consumer", "Raiders.Fixture.Foundation", "1.0.0");
        AssertLockVersion("Consumer", "Raiders.Fixture.Feature", "1.0.0");
        Success(Restore(locked: true));
        Assert.AreEqual("one", RunConsumer());
    }

    [TestMethod]
    public void RepackingIdenticalPayloadIsIdempotentAndDifferentPayloadRequiresNewVersion()
    {
        Success(Restore());
        var original = SHA256.HashData(File.ReadAllBytes(Package("Foundation")));
        Success(Restore(locked: true));
        CollectionAssert.AreEqual(original,
                                  SHA256.HashData(File.ReadAllBytes(Package("Foundation"))));
        Write("Foundation/Value.cs", "namespace Fixture; public static class Value { public " +
                                         "static string Read() => \"two\"; }");
        var collision = Restore(locked: true);
        Assert.AreNotEqual(0, collision.Code);
        StringAssert.Contains(collision.Output, "Immutable package collision");
        CollectionAssert.AreEqual(original,
                                  SHA256.HashData(File.ReadAllBytes(Package("Foundation"))));
        Assert.AreEqual("one", RunConsumer());

        Write("Foundation/Foundation.csproj", Library("Foundation", version: "1.0.1"));
        Write("Feature/Feature.csproj", Library("Feature", "Foundation", "1.0.1"));
        PinVersions("1.0.1");
        var locked = Restore(locked: true);
        Assert.AreNotEqual(0, locked.Code);
        StringAssert.Contains(locked.Output, "NU1004");
        Success(Restore());
        AssertLockVersion("Consumer", "Raiders.Fixture.Foundation", "1.0.1");
        AssertLockVersion("Consumer", "Raiders.Fixture.Feature", "1.0.1");
        Assert.IsTrue(File.Exists(Package("Foundation", "1.0.1")));
        Assert.IsTrue(
            Directory.Exists(Path.Combine(root, "cache", "raiders.fixture.foundation", "1.0.0")));
        Success(Restore(locked: true));
        Assert.AreEqual("two", RunConsumer());
    }

    [TestMethod]
    public void ProducerVersionMustMatchConsumerCentralPin()
    {
        Write("Foundation/Foundation.csproj", Library("Foundation", version: "1.0.1"));
        var result = Restore();
        Assert.AreNotEqual(0, result.Code);
        StringAssert.Contains(result.Output,
                              "must match a central PackageVersion and PackageReference");
        Assert.IsFalse(File.Exists(Package("Feature")));
    }

    [TestMethod]
    public void CyclicProducerGraphFailsInsteadOfRecursingForever()
    {
        Write("Foundation/Foundation.csproj", Library("Foundation", "Feature"));
        var result = Restore();
        Assert.AreNotEqual(0, result.Code);
        StringAssert.Contains(result.Output, "MSB4006");
    }

    [TestMethod]
    public void LockedRestoreRejectsMissingLock()
    {
        var result = Restore(locked: true);
        Assert.AreNotEqual(0, result.Code);
        StringAssert.Contains(result.Output, "Commit a reviewed packages.lock.json");
    }

    [TestMethod]
    [DataRow("1.*")]
    [DataRow("[1.0.0,2.0.0)")]
    public void InternalVersionsCannotFloat(string version)
    {
        PinVersions(version);
        var result = Restore();
        Assert.AreNotEqual(0, result.Code);
        StringAssert.Contains(result.Output, "explicit immutable versions");
    }

    [TestMethod]
    public void ExecutablesCannotBeSelectedAsLibraryProducers()
    {
        var result = Dotnet("msbuild", "Consumer/Consumer.csproj", "-t:RaidersPackLocal",
                            "-p:RestoreLockedMode=false");
        Assert.AreNotEqual(0, result.Code);
        StringAssert.Contains(result.Output, "packable libraries only");
    }

    private void PinVersions(string version)
    {
        Write("Directory.Packages.props", $@"<Project>
    <PropertyGroup><ManagePackageVersionsCentrally>true</ManagePackageVersionsCentrally><CentralPackageVersionOverrideEnabled>false</CentralPackageVersionOverrideEnabled></PropertyGroup>
    <ItemGroup>
        <PackageVersion Include=""Raiders.Fixture.Foundation"" Version=""{version}"" />
        <PackageVersion Include=""Raiders.Fixture.Feature"" Version=""{version}"" />
    </ItemGroup>
</Project>");
    }

    private static string Library(string name, string? dependency = null, string version = "1.0.0")
    {
        string reference = dependency == null ? "" : $@"<ItemGroup>
    <PackageReference Include=""Raiders.Fixture.{dependency}"" />
    <RaidersPackageProject Include=""../{dependency}/{dependency}.csproj"" />
</ItemGroup>";
        return $@"<Project Sdk=""Microsoft.NET.Sdk"">
    <PropertyGroup><PackageId>Raiders.Fixture.{name}</PackageId><Version>{version}</Version><IsPackable>true</IsPackable></PropertyGroup>
    {reference}
</Project>";
    }

    private string Package(string name, string version = "1.0.0")
    {
        return Path.Combine(feed, $"Raiders.Fixture.{name}.{version}.nupkg");
    }

    private (int Code, string Output) Restore(bool locked = false)
    {
        return Dotnet("msbuild", "Consumer/Consumer.csproj", "-t:RaidersRestore",
                      $"-p:RestoreLockedMode={locked.ToString().ToLowerInvariant()}");
    }

    private string RunConsumer()
    {
        Success(Dotnet("build", "Consumer/Consumer.csproj", "--no-restore"));
        var result = Dotnet(Path.Combine(root, "Consumer/bin/Debug/net10.0/Consumer.dll"));
        Success(result);
        return result.Output.Trim();
    }

    private void AssertLockVersion(string project, string id, string version, bool expected = true)
    {
        using var document =
            JsonDocument.Parse(File.ReadAllText(Path.Combine(root, project, "packages.lock.json")));
        var dependencies = document.RootElement.GetProperty("dependencies").GetProperty("net10.0");
        bool present = dependencies.TryGetProperty(id, out var entry);
        Assert.AreEqual(expected, present);
        if (present)
        {
            Assert.AreEqual(version, entry.GetProperty("resolved").GetString());
        }
    }

    private void Write(string relative, string content)
    {
        string path = Path.Combine(root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }

    private (int Code, string Output) Dotnet(params string[] arguments)
    {
        var start = new ProcessStartInfo(
            "dotnet") { WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
                        RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in arguments)
        {
            start.ArgumentList.Add(argument);
        }

        if (arguments[0] is "msbuild" or "build")
        {
            start.ArgumentList.Add("-nodeReuse:false");
            start.ArgumentList.Add("-p:ContinuousIntegrationBuild=false");
            start.ArgumentList.Add($"-p:RaidersLocalPackageFeed={feed}");
        }

        start.Environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1";
        start.Environment["DOTNET_NOLOGO"] = "1";
        using var process = Process.Start(start)!;
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(120_000))
        {
            process.Kill(entireProcessTree: true);
            process.WaitForExit();
            Assert.Fail($"Build graph timed out in {root}");
        }

        return (process.ExitCode,
                stdout.GetAwaiter().GetResult() + stderr.GetAwaiter().GetResult());
    }

    private static void Success((int Code, string Output)result)
    {
        Assert.AreEqual(0, result.Code, result.Output);
    }
}
