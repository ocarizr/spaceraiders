using System.Text.Json;
using System.Text.Json.Serialization;
using Raiders.Diagnostics;
using Raiders.Environment.Core;

namespace Raiders.Environment.Setup;

public sealed record HeadlessProfile(string Id, string Title, string[] DependsOn);
public sealed record HeadlessResult(int SchemaVersion, string Command, string Status, int ExitCode,
                                    EnvironmentHealth? Health = null,
                                    RecipeStepResult[]? Steps = null, string? Error = null,
                                    HeadlessProfile[]? Profiles = null, string[]? Usage = null);

/// <summary>One JSON result on stdout; bounded structured diagnostics on stderr.</summary>
public static class HeadlessCommands
{
    private static readonly JsonSerializerOptions JsonOptions =
        new() { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
                Converters = { new JsonStringEnumConverter() } };

    public static async Task<int> RunAsync(
        string[] args, TextWriter output, TextWriter error,
        Func<string, TelemetrySession, Log, ISetupBackend>? createBackend = null,
        CancellationToken cancellationToken = default)
    {
        var command = "invalid";
        var telemetry = new TelemetrySession("setup.headless." + Guid.NewGuid().ToString("N"));
        var log = new Log(telemetry, error);
        var scope = new ProfileScope();
        ProfileScope.Begin(ref scope, telemetry, "setup", "headless-command");
        using (scope)
        {
            HeadlessResult result;
            try
            {
                var options = Parse(args);
                command = options.Command;
                using var deadline =
                    CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                deadline.CancelAfter(TimeSpan.FromSeconds(options.TimeoutSeconds));
                try
                {
                    if (command == "help")
                    {
                        result = new(1, command, "Completed", 0, Usage: [
                            "profiles [--state-root ABSOLUTE_PATH]",
                            "doctor --profile ID [--profile ID ...] [--acknowledge-actions] " +
                                "[--state-root ABSOLUTE_PATH] [--timeout-seconds 1..7200]",
                            "prepare --profile ID [--profile ID ...] --yes [--state-root " +
                                "ABSOLUTE_PATH] [--timeout-seconds 1..7200]"
                        ]);
                    }
                    else
                    {
                        deadline.Token.ThrowIfCancellationRequested();
                        var backend = (createBackend ??
                                       ((root, session, sink) => new WindowsSetupBackend(
                                            root, session, sink)))(options.Root, telemetry, log);
                        var catalog = EnvironmentProbe.ReadCatalog(backend.CatalogJson);
                        if (options.Profiles.Any(
                                id => !catalog.Profiles.Any(profile => profile.Id == id)))
                        {
                            throw new CommandLineException(
                                "Select known profile IDs; use profiles to list them.");
                        }

                        if (command == "profiles")
                        {
                            result =
                                new(1, command, "Completed", 0,
                                    Profiles: catalog.Profiles
                                        .Select(profile => new HeadlessProfile(
                                                    profile.Id, profile.Title, profile.DependsOn))
                                        .ToArray());
                        }
                        else if (command == "doctor")
                        {
                            if (options.AcknowledgeActions && !backend.SupportsRepair)
                            {
                                throw new CommandLineException(
                                    "This host does not support installer-action reconciliation.");
                            }

                            var health =
                                options.AcknowledgeActions
                                    ? await backend.ReconcileAsync(options.Profiles, deadline.Token)
                                    : await backend.ScanAsync(options.Profiles, deadline.Token);
                            var ready = health.State == "Ready";
                            result = new(1, command, ready ? "Completed" : "Blocked", ready ? 0 : 2,
                                         Shareable(health));
                        }
                        else
                        {
                            var progress = new ProgressSink(log);
                            var preparation = await backend.PrepareAsync(options.Profiles, progress,
                                                                         deadline.Token);
                            var code = preparation.Status switch {
                                PreparationStatus.Completed when preparation.Health.State ==
                                    "Ready" => 0,
                                PreparationStatus.Failed => 3,
                                _ => 2
                            };
                            result = new(1, command,
                                         code == 0   ? "Completed"
                                         : code == 3 ? "Failed"
                                                     : "Blocked",
                                         code, Shareable(preparation.Health), preparation.Steps);
                        }
                    }

                    deadline.Token.ThrowIfCancellationRequested();
                }
                catch (OperationCanceledException) when (deadline.IsCancellationRequested)
                {
                    result = new(1, command, "Cancelled", 130,
                                 Error: ("Operation cancelled or its deadline expired. Scan " +
                                         "again before retrying."));
                }
            }
            catch (CommandLineException exception)
            {
                result = new(1, command, "InvalidArguments", 64, Error: exception.Message);
            }
            catch (Exception)
            {
                result = new(1, command, "Error", 70,
                             Error: ("Setup could not complete. Check state storage, permissions " +
                                     "and other running setup instances; preserve repair records " +
                                     "for diagnosis."));
            }

            log.Write(LogLevel.Info, "setup", () => "Headless command finished.",
                      () => new Dictionary<string, object?> { ["command"] = command,
                          ["status"] = result.Status, ["exit_code"] = result.ExitCode });

            await output.WriteLineAsync(JsonSerializer.Serialize(result, JsonOptions));
            await output.FlushAsync();
            await error.FlushAsync();
            return result.ExitCode;
        }
    }

    private static EnvironmentHealth Shareable(EnvironmentHealth health)
    {
        return health with {
            Visibility = "shareable",
            Profiles =
                health.Profiles
                    .Select(profile => profile with {
                        Requirements = profile.Requirements
                                           .Select(requirement => requirement with {
                                               Evidence = requirement.Evidence
                                                              .Select(evidence => evidence with {
                                                                  Location = new EvidenceLocation(
                                                                      "redacted", null)
                                                              })
                                                              .ToArray()
                                           })
                                           .ToArray()
                    })
                    .ToArray()
        };
    }

    private sealed class CommandLineException(string message) : ArgumentException
    (message);
    private sealed record Options(string Command, string[] Profiles, string Root,
                                  bool AcknowledgeActions, int TimeoutSeconds);

    private static Options Parse(string[] args)
    {
        if (args.Length == 1 && args[0] is "--help" or "help")
        {
            return new("help", [], "", false, 300);
        }

        if (args.Length == 0 || args[0] is not("doctor" or "prepare" or "profiles"))
        {
            throw new CommandLineException(
                "Expected doctor, prepare or profiles. Use --help for syntax.");
        }

        var profiles = new List<string>();
        var root = Path.Combine(
            System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData),
            "Raiders", "Setup");
        var acknowledged = false;
        var yes = false;
        var timeout = 300;
        var seen = new HashSet<string>();
        for (var index = 1; index < args.Length; index++)
        {
            var option = args[index];
            if (option != "--profile" && !seen.Add(option))
            {
                throw new CommandLineException(
                    "Do not repeat command options other than --profile.");
            }

            if (option == "--yes")
            {
                yes = true;
            }
            else if (option == "--acknowledge-actions")
            {
                acknowledged = true;
            }
            else if (option is "--profile" or "--state-root" or "--timeout-seconds")
            {
                if (++index >= args.Length || string.IsNullOrWhiteSpace(args[index]) ||
                    args[index].StartsWith("--", StringComparison.Ordinal))
                {
                    throw new CommandLineException("A command option is missing its value.");
                }

                var value = args[index];
                if (option == "--profile")
                {
                    profiles.Add(value);
                }
                else if (option == "--state-root")
                {
                    if (!Path.IsPathFullyQualified(value))
                    {
                        throw new CommandLineException(
                            "--state-root requires an absolute owned directory.");
                    }

                    root = Path.GetFullPath(value);
                }
                else if (!int.TryParse(value, out timeout) || timeout is < 1 or > 7200)
                {
                    throw new CommandLineException("--timeout-seconds must be between 1 and 7200.");
                }
            }
            else
            {
                throw new CommandLineException("Unknown command option. Use --help for syntax.");
            }
        }

        if ((args[0] == "profiles" && profiles.Count != 0) ||
            (args[0] != "profiles" && profiles.Count == 0) || (args[0] == "prepare" && !yes) ||
            (args[0] != "prepare" && yes) || (args[0] != "doctor" && acknowledged))
        {
            throw new CommandLineException("Select profiles explicitly; prepare requires --yes, " +
                                           "and only doctor accepts --acknowledge-actions.");
        }

        return new(args[0], profiles.Distinct().ToArray(), root, acknowledged, timeout);
    }

    private sealed class ProgressSink(Log log) : IProgress<PreparationProgress>
    {
        public void Report(PreparationProgress value)
        {
            log.Write(LogLevel.Info, "setup", () => "Preparation progress.",
                      () => new Dictionary<string, object?> { ["run_id"] = value.RunId,
                          ["profile_id"] = value.ProfileId, ["requirement_id"] = value.RequirementId,
                          ["stage"] = value.Stage.ToString() });
        }
    }
}
