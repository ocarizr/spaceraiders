using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Raiders.Environment.Core;

public sealed record ToolCandidate(string? Version, string Architecture, string? Fingerprint = null,
                                   string? Location = null, string? Problem = null);
public sealed record DiscoveryResult(string Status, ToolCandidate[] Candidates,
                                     string? ExpectedFingerprint = null);
public interface IToolDiscovery
{
    ValueTask<DiscoveryResult> DiscoverAsync(ToolRequirement requirement,
                                             CancellationToken cancellationToken);
}

public enum ToolProtocol
{
    Msvc,
    CMake,
    Ninja,
    DotnetSdk,
    Python,
    LuaJit
}

public sealed record ProbeCommand(string Executable, string[] Arguments);
public sealed record CommandResult(int ExitCode, string Output, string Error);

/// <summary>
/// Trusted application registrations, never commands taken from a catalog. Candidate
/// paths must be a complete search scope supplied by the Windows discovery host.
/// Missing adapters/manifests remain Unknown. Registration is frozen by convention
/// before scanning; callers must not mutate this registry during a scan.
/// </summary>
public sealed class ToolDiscovery : IToolDiscovery
{
    private readonly Dictionary<
        string, Func<ToolRequirement, CancellationToken, ValueTask<DiscoveryResult>>> probes =
        new();
    private readonly Func<ProbeCommand, CancellationToken, Task<CommandResult>> execute;

    public ToolDiscovery(Func<ProbeCommand, CancellationToken, Task<CommandResult>>? execute = null)
    {
        this.execute = execute ?? RunAsync;
    }

    public ValueTask<DiscoveryResult> DiscoverAsync(ToolRequirement requirement,
                                                    CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return probes.TryGetValue(requirement.ProbeId, out var probe)
                   ? probe(requirement, cancellationToken)
                   : ValueTask.FromResult(new DiscoveryResult("unresolved", []));
    }

    /// <summary>Register explicit executable candidates; no PATH-only presence check proves
    /// health.</summary>
    public void RegisterExecutable(string probeId, ToolProtocol protocol,
                                   params string[] candidates)
    {
        if (!Enum.IsDefined(protocol) || candidates.Length == 0 ||
            candidates.Any(path => !Path.IsPathFullyQualified(path)))
        {
            throw new ArgumentException("Executable discovery needs absolute candidate paths.");
        }

        var paths = candidates.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        probes.Add(
            probeId,
            async (requirement, token) =>
            {
                var kind = protocol switch { ToolProtocol.Msvc => "native-toolchain",
                                             ToolProtocol.DotnetSdk => "managed-sdk",
                                             ToolProtocol.Python => "python-interpreter",
                                             ToolProtocol.LuaJit => "lua-toolchain",
                                             _ => "build-tool" };
                if (requirement.Kind != kind)
                {
                    return new("unresolved", []);
                }

                var found = new List<ToolCandidate>();
                var failed = false;
                foreach (var path in paths)
                {
                    token.ThrowIfCancellationRequested();
                    string architecture;
                    try
                    {
                        using var stream = File.OpenRead(path);
                        using var pe = new PEReader(stream);
                        architecture = pe.PEHeaders.CoffHeader.Machine switch {
                            Machine.Amd64 => "x64", Machine.I386 => "x86", Machine.Arm64 => "arm64",
                            _ => "unknown"
                        };
                    }
                    catch (FileNotFoundException)
                    {
                        continue;
                    }
                    catch (DirectoryNotFoundException)
                    {
                        continue;
                    }
                    catch (Exception error)
                        when (error is IOException or UnauthorizedAccessException or
                                  BadImageFormatException)
                    {
                        failed = true;
                        continue;
                    }

                    var arguments = protocol switch {
                        ToolProtocol.DotnetSdk => new[] { "--list-sdks" },
                        ToolProtocol
                            .Python => ["-I", "-c",
                                        "import sys,struct,json; " +
                                            "print(json.dumps({'version':'.'.join(map(str,sys." +
                                            "version_info[:3])),'bits':struct.calcsize('P')*8}))"],
                        ToolProtocol.LuaJit => ["-v"], ToolProtocol.Msvc => [],
                        _ => ["--version"]
                    };
                    var output = await execute(new(path, arguments), token);
                    var parsed = Parse(protocol, output, architecture, path);
                    if (protocol == ToolProtocol.Msvc && parsed.Status == "found" &&
                        Version.TryParse(parsed.Candidates[0].Version, out var compilerVersion) &&
                        compilerVersion.Revision == -1)
                    {
                        var metadata = FileVersionInfo.GetVersionInfo(path);
                        if (metadata.FileMajorPart != compilerVersion.Major ||
                            metadata.FileMinorPart != compilerVersion.Minor ||
                            metadata.FileBuildPart != compilerVersion.Build)
                        {
                            return new("failed", []);
                        }

                        parsed = parsed with {
                            Candidates = [parsed.Candidates[0] with {
                                Version =
                                    $"{metadata.FileMajorPart}.{metadata.FileMinorPart}.{metadata.FileBuildPart}.{metadata.FilePrivatePart}"
                            }]
                        };
                    }

                    if (parsed.Status == "found")
                    {
                        found.AddRange(parsed.Candidates);
                    }
                    else if (parsed.Status != "absent")
                    {
                        failed = true;
                    }
                }

                // An incomplete search cannot prove that all candidates are incompatible.
                return failed ? new("failed", [])
                              : new(found.Count > 0 ? "found" : "absent", found.ToArray());
            });
    }

    /// <summary>
    /// Verify a reviewed offline manifest on every scan: SDK/toolset, Lua bundle,
    /// package closure (config, locks and all packages) or designer artifacts. Paths
    /// are relative to one owned root; hashes attest version/architecture metadata.
    /// Does not contact feeds, restore packages, or trust directory existence.
    /// </summary>
    public void RegisterManifest(string probeId, string kind, string? reference, string root,
                                 string? version, string architecture,
                                 IReadOnlyDictionary<string, string> files)
    {
        if (!Path.IsPathFullyQualified(root) || files.Count == 0 ||
            architecture is not("x64" or "x86" or "arm64" or "any") ||
            version is not null && !EnvironmentProbe.VersionText.IsMatch(version))
        {
            throw new ArgumentException("Invalid approved manifest.");
        }

        var approved = files.OrderBy(item => item.Key, StringComparer.Ordinal).ToArray();
        foreach (var (path, hash) in approved)
        {
            if (Path.IsPathRooted(path) || path.Contains('\\') ||
                path.Split('/').Any(part => part is "" or "." or "..") ||
                path.Contains(':') || !EnvironmentProbe.Sha256.IsMatch(hash))
            {
                throw new ArgumentException(
                    "Manifest requires safe relative paths and SHA-256 hashes.");
            }
        }

        var expected = Fingerprint(approved, version, architecture);
        probes.Add(
            probeId,
            async (requirement, token) =>
            {
                if (requirement.Kind != kind || requirement.Constraint.Reference != reference)
                {
                    return new("unresolved", []);
                }

                try
                {
                    var attributes = File.GetAttributes(root);
                    if (!attributes.HasFlag(FileAttributes.Directory) ||
                        attributes.HasFlag(FileAttributes.ReparsePoint))
                    {
                        return new("failed", []);
                    }
                }
                catch (FileNotFoundException)
                {
                    return new("absent", []);
                }
                catch (DirectoryNotFoundException)
                {
                    return new("absent", []);
                }

                var observed = new List<KeyValuePair<string, string>>();
                foreach (var (relative, _) in approved)
                {
                    token.ThrowIfCancellationRequested();
                    try
                    {
                        // Refuse links at any level rather than following a path outside the root.
                        var path = root;
                        foreach (var segment in relative.Split('/'))
                        {
                            path = Path.Combine(path, segment);
                            if (File.GetAttributes(path).HasFlag(FileAttributes.ReparsePoint))
                            {
                                return new("failed", []);
                            }
                        }

                        await using var stream = File.OpenRead(path);
                        var hash =
                            Convert.ToHexStringLower(await SHA256.HashDataAsync(stream, token));
                        observed.Add(new(relative, hash));
                    }
                    catch (Exception error)
                        when (error is FileNotFoundException or DirectoryNotFoundException)
                    {
                        return new("found",
                                   [new(version, architecture, null, root, "IncompleteSetup")],
                                   expected);
                    }
                }

                var fingerprint = Fingerprint(observed, version, architecture);
                return new("found", [new(version, architecture, fingerprint, root,
                                         fingerprint == expected ? null : "StateStale")],
                           expected);
            });
    }

    /// <summary>
    /// Map a fresh Q07 check result; the caller must verify the approved checker,
    /// lock and local wheel identity before calling. Never pass a saved health receipt.
    /// Q07 reasons/paths are deliberately excluded from diagnostic evidence.
    /// </summary>
    public static DiscoveryResult PythonEnvironmentResult(string json, int exitCode, string profile,
                                                          string expectedFingerprint,
                                                          string observedFingerprint)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.GetProperty("profile").GetString() != profile ||
            profile is not("build" or "tools"))
        {
            return new("failed", []);
        }

        return (root.GetProperty("status").GetString(), exitCode) switch {
            ("Ready", 0) =>
                new("found", [new(null, "x64", observedFingerprint)], expectedFingerprint),
            ("Missing", 1) => new("absent", []),
            ("Needs repair", 1) =>
                new("found", [new(null, "x64", observedFingerprint, Problem: "StateStale")],
                    expectedFingerprint),
            _ => new("failed", [])
        };
    }

    /// <summary>Adapters may wrap fresh read-only platform or Q07 checks; preparation is
    /// Y03.</summary>
    public void Register(string probeId,
                         Func<ToolRequirement, CancellationToken, ValueTask<DiscoveryResult>> probe)
    {
        ArgumentNullException.ThrowIfNull(probe);
        probes.Add(probeId, probe);
    }

    private static string Fingerprint(IEnumerable<KeyValuePair<string, string>> files,
                                      string? version, string architecture)
    {
        var text = JsonSerializer.Serialize(
            new { version, architecture,
                  files = files.Select(item => new[] { item.Key, item.Value }).ToArray() });
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }

    private static DiscoveryResult Parse(ToolProtocol protocol, CommandResult result,
                                         string architecture, string path)
    {
        if (result.ExitCode != 0 && !(protocol == ToolProtocol.Msvc && result.ExitCode == 2))
        {
            return new("failed", []);
        }

        if (protocol == ToolProtocol.Python)
        {
            using var json = JsonDocument.Parse(result.Output);
            var bits = json.RootElement.GetProperty("bits").GetInt32();
            var consistent = architecture == "x86" && bits == 32 ||
                             architecture is "x64" or "arm64" && bits == 64;
            return !consistent
                       ? new("failed", [])
                       : new("found", [new(json.RootElement.GetProperty("version").GetString(),
                                           architecture, Location: path)]);
        }

        if (protocol == ToolProtocol.DotnetSdk)
        {
            var lines = result.Output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries);
            var sdks = new List<ToolCandidate>();
            foreach (var line in lines)
            {
                var match =
                    Regex.Match(line, "^([0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?) \\[.+\\]$");
                if (!match.Success)
                {
                    return new("failed", []);
                }

                sdks.Add(new(match.Groups[1].Value, architecture, Location: path));
            }

            return new(sdks.Count == 0 ? "absent" : "found", sdks.ToArray());
        }

        var text = result.Output + "\n" + result.Error;
        var pattern = protocol switch {
            ToolProtocol.Msvc => "Compiler Version ([0-9]+(?:\\.[0-9]+){2,3}) for (x64|x86|ARM64)",
            ToolProtocol.CMake =>
                "(?m)^cmake version ([0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?)\\s*$",
            ToolProtocol.Ninja => "(?m)^([0-9]+\\.[0-9]+\\.[0-9]+)\\s*$",
            _ => "(?m)^LuaJIT ([0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?)(?:\\s|$)"
        };
        var version = Regex.Match(text, pattern);
        if (!version.Success)
        {
            return new("failed", []);
        }

        // The compiler's PE machine describes its host; the banner describes its target.
        if (protocol == ToolProtocol.Msvc)
        {
            architecture = version.Groups[2].Value.ToLowerInvariant();
        }

        return new("found", [new(version.Groups[1].Value, architecture, Location: path)]);
    }

    private static async Task<CommandResult> RunAsync(ProbeCommand command, CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromSeconds(15));
        using var process = new Process { StartInfo = new ProcessStartInfo(command.Executable) {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true,
            RedirectStandardError = true
        } };
        foreach (var argument in command.Arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        process.Start();
        try
        {
            var output = ReadBoundedAsync(process.StandardOutput, deadline.Token);
            var error = ReadBoundedAsync(process.StandardError, deadline.Token);
            await Task.WhenAll(output, error, process.WaitForExitAsync(deadline.Token));
            return new(process.ExitCode, await output, await error);
        }
        catch (OperationCanceledException) when (!token.IsCancellationRequested)
        {
            throw new TimeoutException("Environment probe exceeded its deadline.");
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
    }

    private static async Task<string> ReadBoundedAsync(StreamReader reader, CancellationToken token)
    {
        var buffer = new char[65537];
        var size = 0;
        while (size < buffer.Length)
        {
            var read = await reader.ReadAsync(buffer.AsMemory(size), token);
            if (read == 0)
            {
                return new string(buffer, 0, size);
            }

            size += read;
        }

        throw new IOException("Probe output exceeded the limit.");
    }
}
