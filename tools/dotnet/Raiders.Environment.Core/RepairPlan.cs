using System.Text.Json;
using System.Text.RegularExpressions;

namespace Raiders.Environment.Core;

public sealed record RepairRecord(string CatalogId, string ProfileId, string RequirementId,
                                  string PolicySha256, PreparationStage Stage,
                                  PreparationReason Reason);

/// <summary>Local interruption evidence, never a receipt proving that a tool is ready.</summary>
public sealed class RepairPlan
{
    private readonly string directory;
    private RepairRecord[] observed = [];
    public IReadOnlyList<RepairRecord> LastObserved => observed.ToArray();

    public RepairPlan(string directory)
    {
        if (!Path.IsPathFullyQualified(directory))
        {
            throw new ArgumentException("Repair storage requires an absolute owned directory.");
        }

        this.directory = Path.GetFullPath(directory);
    }

    internal Session Open()
    {
        ArtifactAcquisition.RejectLinks(directory);
        Directory.CreateDirectory(directory);
        var lockPath = Path.Combine(directory, "repair.lock");
        ArtifactAcquisition.RejectLinks(lockPath);
        var lease =
            new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        try
        {
            return new Session(this, lease);
        }
        catch
        {
            lease.Dispose();
            throw;
        }
    }

    private sealed record Document(int Version, RepairRecord[] Entries);

    internal sealed class Session : IDisposable
    {
        private readonly RepairPlan owner;
        private readonly FileStream lease;
        private readonly string path;
        internal List<RepairRecord> Entries { get; }

        internal Session(RepairPlan owner, FileStream lease)
        {
            this.owner = owner;
            this.lease = lease;
            path = Path.Combine(owner.directory, "repair-state.json");
            ArtifactAcquisition.RejectLinks(path);
            try
            {
                using var stream = File.OpenRead(path);
                if (stream.Length > 1048576)
                {
                    throw new InvalidDataException("Repair state exceeds its limit.");
                }

                var document = JsonSerializer.Deserialize<Document>(stream);
                if (document is not { Version : 1, Entries : not null } ||
                    document.Entries.Length > 2048 ||
                    document.Entries.Any(
                        entry => entry is null || !ValidId(entry.CatalogId) ||
                                 !ValidId(entry.ProfileId) || !ValidId(entry.RequirementId) ||
                                 entry.PolicySha256 is null ||
                                 !EnvironmentProbe.Sha256.IsMatch(entry.PolicySha256) ||
                                 !Enum.IsDefined(entry.Stage) || !Enum.IsDefined(entry.Reason)) ||
                    document.Entries
                            .DistinctBy(entry =>
                                            (entry.CatalogId, entry.ProfileId, entry.RequirementId))
                            .Count() != document.Entries.Length)
                {
                    throw new InvalidDataException(
                        "Repair state is invalid; preserve it for diagnosis.");
                }

                Entries = document.Entries.ToList();
            }
            catch (FileNotFoundException)
            {
                Entries = [];
            }

            owner.observed = Entries.ToArray();
        }

        internal void Set(RepairRecord entry)
        {
            Entries.RemoveAll(value => value.CatalogId == entry.CatalogId &&
                                       value.ProfileId == entry.ProfileId &&
                                       value.RequirementId == entry.RequirementId);

            Entries.Add(entry);
            Save();
        }

        internal void Remove(string catalog, string profile, string requirement)
        {
            if (Entries.RemoveAll(value => value.CatalogId == catalog &&
                                           value.ProfileId == profile &&
                                           value.RequirementId == requirement) > 0)
            {
                Save();
            }
        }

        private void Save()
        {
            ArtifactAcquisition.RejectLinks(path);
            var temporary =
                Path.Combine(owner.directory, Guid.NewGuid().ToString("N") + ".partial");
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write,
                                                   FileShare.None))
                {
                    JsonSerializer.Serialize(stream, new Document(1, Entries.ToArray()));
                    stream.Flush(flushToDisk: true);
                }

                File.Move(temporary, path, overwrite: true);
                owner.observed = Entries.ToArray();
            }
            finally
            {
                File.Delete(temporary);
            }
        }

        private static bool ValidId(string? id)
        {
            return id is not null && Regex.IsMatch(id, "^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$");
        }

        public void Dispose()
        {
            lease.Dispose();
        }
    }
}
