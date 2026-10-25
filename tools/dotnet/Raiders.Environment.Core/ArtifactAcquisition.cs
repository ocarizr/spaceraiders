using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Raiders.Environment.Core;

public sealed record SetupArtifact(string Id, Uri Source, string FileName, string Sha256,
                                   long MaximumBytes = 536870912);
public sealed record VerifiedArtifact(string Id, string Path, string Sha256);

public sealed class ArtifactIntegrityException() : IOException
("Artifact integrity verification failed.");

/// <summary>Checksum-verified acquisition into a caller-owned cache. No extraction or
/// execution.</summary>
public sealed class ArtifactAcquisition
{
    private static readonly HttpClient SharedClient = new() { Timeout = TimeSpan.FromMinutes(2) };
    private readonly string root;
    private readonly HttpClient client;

    public ArtifactAcquisition(string cacheDirectory, HttpClient? client = null)
    {
        if (!Path.IsPathFullyQualified(cacheDirectory))
        {
            throw new ArgumentException("Artifact cache must have an absolute path.");
        }

        root = Path.GetFullPath(cacheDirectory);
        this.client = client ?? SharedClient;
    }

    public async Task<VerifiedArtifact> AcquireAsync(SetupArtifact artifact,
                                                     CancellationToken cancellationToken = default)
    {
        Validate(artifact);
        cancellationToken.ThrowIfCancellationRequested();
        var directory = Path.Combine(root, artifact.Sha256);
        RejectLinks(directory);
        Directory.CreateDirectory(directory);
        var destination = Path.Combine(directory, artifact.FileName);
        RejectLinks(destination);
        try
        {
            await VerifyAsync(destination, artifact, cancellationToken);
            return new(artifact.Id, destination, artifact.Sha256);
        }
        catch (FileNotFoundException)
        {
            // Only conclusive absence permits acquisition. Corrupt cache entries are rejected.
        }

        var temporary = Path.Combine(directory, Guid.NewGuid().ToString("N") + ".partial");
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            deadline.CancelAfter(TimeSpan.FromMinutes(5));
            await using (var source = await OpenAsync(artifact.Source, deadline.Token))
            {
                await using var target =
                    new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                                   81920, FileOptions.Asynchronous);
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                var buffer = new byte[81920];
                long total = 0;
                int read;
                while ((read = await source.ReadAsync(buffer, deadline.Token)) != 0)
                {
                    total = checked(total + read);
                    if (total > artifact.MaximumBytes)
                    {
                        throw new ArtifactIntegrityException();
                    }

                    hash.AppendData(buffer, 0, read);
                    await target.WriteAsync(buffer.AsMemory(0, read), deadline.Token);
                }

                if (Convert.ToHexStringLower(hash.GetHashAndReset()) != artifact.Sha256)
                {
                    throw new ArtifactIntegrityException();
                }
            }

            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                File.Move(temporary, destination, overwrite: false);
            }
            catch (IOException) when (File.Exists(destination))
            {
                // Another acquisition may have published the same hash. Never overwrite it.
                await VerifyAsync(destination, artifact, cancellationToken);
            }

            return new(artifact.Id, destination, artifact.Sha256);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    internal static void Validate(SetupArtifact artifact)
    {
        ArgumentNullException.ThrowIfNull(artifact);
        if (!Regex.IsMatch(artifact.Id, "^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$") ||
            artifact.Source is not { IsAbsoluteUri : true } ||
            artifact.Source.Scheme is not("https" or "file") || artifact.Source.IsUnc ||
            artifact.Source.UserInfo.Length != 0 ||
            !EnvironmentProbe.Sha256.IsMatch(artifact.Sha256) || artifact.MaximumBytes <= 0 ||
            !Regex.IsMatch(artifact.FileName, "^[A-Za-z0-9][A-Za-z0-9._-]{0,179}$") ||
            artifact.FileName.EndsWith('.') ||
            Regex.IsMatch(artifact.FileName, "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                          RegexOptions.IgnoreCase))
        {
            throw new ArgumentException("Invalid approved artifact descriptor.");
        }
    }

    private async Task<Stream> OpenAsync(Uri source, CancellationToken token)
    {
        if (source.IsFile)
        {
            return new FileStream(source.LocalPath, FileMode.Open, FileAccess.Read, FileShare.Read,
                                  81920, FileOptions.Asynchronous | FileOptions.SequentialScan);
        }

        return await client.GetStreamAsync(source, token);
    }

    private static async Task VerifyAsync(string path, SetupArtifact artifact,
                                          CancellationToken token)
    {
        RejectLinks(path);
        // Atomic publication briefly holds rename/delete access on Windows.
        await using var stream =
            new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete,
                           81920, FileOptions.Asynchronous | FileOptions.SequentialScan);
        if (stream.Length > artifact.MaximumBytes ||
            Convert.ToHexStringLower(await SHA256.HashDataAsync(stream, token)) != artifact.Sha256)
        {
            throw new ArtifactIntegrityException();
        }
    }

    private static void RejectLinks(string path)
    {
        for (string? current = path; current is not null; current = Path.GetDirectoryName(current))
        {
            try
            {
                if (File.GetAttributes(current).HasFlag(FileAttributes.ReparsePoint))
                {
                    throw new IOException("Artifact cache must not contain links.");
                }
            }
            catch (FileNotFoundException)
            {
            }
            catch (DirectoryNotFoundException)
            {
            }
        }
    }
}
