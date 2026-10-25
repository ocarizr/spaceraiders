using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using Microsoft.Build.Framework;

public sealed class RaidersPackageArtifacts : Microsoft.Build.Utilities.Task
{
    private string candidate;

    public string Candidate
    {
        get {
            return candidate;
        }

        set {
            candidate = value;
        }
    }

    private string destination;

    public string Destination
    {
        get {
            return destination;
        }

        set {
            destination = value;
        }
    }

    private ITaskItem[] producers;

    public ITaskItem[] Producers
    {
        get {
            return producers;
        }

        set {
            producers = value;
        }
    }

    private ITaskItem[] references;

    public ITaskItem[] References
    {
        get {
            return references;
        }

        set {
            references = value;
        }
    }

    private ITaskItem[] versions;

    public ITaskItem[] Versions
    {
        get {
            return versions;
        }

        set {
            versions = value;
        }
    }

    public override bool Execute()
    {
        if (Producers != null)
        {
            foreach (var producer in Producers)
            {
                string id = producer.GetMetadata("PackageId");
                string version = producer.GetMetadata("PackageVersion");
                var reference =
                    (References ?? Array.Empty<Microsoft.Build.Framework.ITaskItem>())
                        .SingleOrDefault(item => string.Equals(item.ItemSpec, id,
                                                               StringComparison.OrdinalIgnoreCase));
                var pin =
                    (Versions ?? Array.Empty<Microsoft.Build.Framework.ITaskItem>())
                        .SingleOrDefault(item => string.Equals(item.ItemSpec, id,
                                                               StringComparison.OrdinalIgnoreCase));
                if (reference == null || pin == null || pin.GetMetadata("Version") != version)
                {
                    Log.LogError("Selected producer {0} {1} must match a central PackageVersion " +
                                     "and PackageReference.",
                                 id, version);
                }
            }

            return !Log.HasLoggedErrors;
        }

        Func<string, System.Collections.Generic.Dictionary<string, byte[]>> payload = path =>
        {
            using (var stream = File.OpenRead(path))
            {
                using var archive = new ZipArchive(stream, ZipArchiveMode.Read);
                return archive.Entries
                    .Where(entry => !entry.FullName.EndsWith("/") &&
                                    entry.FullName != "[Content_Types].xml" &&
                                    entry.FullName != "_rels/.rels" &&
                                    !entry.FullName.StartsWith(
                                        "package/services/metadata/core-properties/",
                                        StringComparison.Ordinal))
                    .ToDictionary(entry => entry.FullName,
                                  entry =>
                                  {
                                      using (var input = entry.Open())
                                      {
                                          using var output = new MemoryStream();
                                          input.CopyTo(output);
                                          return output.ToArray();
                                      }
                                  },
                                  StringComparer.Ordinal);
            }
        };

        // Rename a complete file on the destination volume; never expose partial packages.
        Directory.CreateDirectory(Path.GetDirectoryName(Destination));
        string temporary = Destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.Copy(Candidate, temporary, false);
            try
            {
                File.Move(temporary, Destination);
            }
            catch (IOException) when (File.Exists(Destination))
            {
                var previous = payload(Destination);
                var current = payload(Candidate);
                if (previous.Count != current.Count ||
                    previous.Any(entry => !current.ContainsKey(entry.Key) ||
                                          !entry.Value.SequenceEqual(current[entry.Key])))
                {
                    Log.LogError("Immutable package collision at {0}. Assign a new version and " +
                                     "update central pins/locks.",
                                 Destination);
                    return false;
                }
            }
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }

        Log.LogMessage(Microsoft.Build.Framework.MessageImportance.High, "Local package ready: {0}",
                       Destination);
        return true;
    }
}
