using System.Net;
using System.Security.Cryptography;
using System.Text;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Raiders.Environment.Core.Tests;

[TestClass]
public sealed class ArtifactAcquisitionTests
{
    private readonly string temporary =
        Path.Combine(Path.GetTempPath(), "raiders-acquisition-" + Guid.NewGuid().ToString("N"));
    private static readonly byte[] Bytes = Encoding.UTF8.GetBytes("approved artifact bytes");

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(temporary))
        {
            Directory.Delete(temporary, recursive: true);
        }
    }

    private static SetupArtifact Descriptor()
    {
        return new("installer", new Uri("https://example.invalid/approved.exe"), "approved.exe",
                   Convert.ToHexStringLower(SHA256.HashData(Bytes)));
    }

    [TestMethod]
    public async Task HttpsArtifactIsVerifiedPublishedAtomicallyAndReusedOffline()
    {
        var requests = 0;
        using var client = new HttpClient(
            new Handler((_, _) =>
                        {
                            requests++;
                            return new(HttpStatusCode.OK) { Content = new ByteArrayContent(Bytes) };
                        }));
        var acquisition = new ArtifactAcquisition(temporary, client);
        var first = await acquisition.AcquireAsync(Descriptor());
        var second = await acquisition.AcquireAsync(Descriptor());
        Assert.AreEqual(first.Path, second.Path);
        Assert.AreEqual(1, requests);
        CollectionAssert.AreEqual(Bytes, await File.ReadAllBytesAsync(first.Path));
        Assert.IsFalse(
            Directory.EnumerateFiles(temporary, "*.partial", SearchOption.AllDirectories).Any());
    }

    [TestMethod]
    public async Task LocalArtifactCopiesRawBytesAndRejectsCorruptCachedBytes()
    {
        Directory.CreateDirectory(temporary);
        var source = Path.Combine(temporary, "source.exe");
        await File.WriteAllBytesAsync(source, Bytes);
        var descriptor = Descriptor() with { Source = new Uri(source) };
        var acquisition = new ArtifactAcquisition(Path.Combine(temporary, "cache"));
        var artifact = await acquisition.AcquireAsync(descriptor);
        CollectionAssert.AreEqual(Bytes, await File.ReadAllBytesAsync(artifact.Path));
        await File.WriteAllBytesAsync(artifact.Path, [1, 2]);
        await Assert.ThrowsAsync<ArtifactIntegrityException>(
            () => acquisition.AcquireAsync(descriptor));

        CollectionAssert.AreEqual(new byte[] { 1, 2 }, await File.ReadAllBytesAsync(artifact.Path));
    }

    [TestMethod]
    [DataRow("hash")]
    [DataRow("size")]
    public async Task InvalidDownloadIsNeverPublished(string failure)
    {
        using var client = new HttpClient(new Handler(
            (_, _) => new(HttpStatusCode.OK) { Content = new ByteArrayContent(Bytes) }));
        var descriptor = failure == "hash" ? Descriptor() with { Sha256 = new string('b', 64) }
                                           : Descriptor() with { MaximumBytes = 2 };
        await Assert.ThrowsAsync<ArtifactIntegrityException>(
            () => new ArtifactAcquisition(temporary, client).AcquireAsync(descriptor));

        Assert.AreEqual(
            0, Directory.EnumerateFiles(temporary, "*", SearchOption.AllDirectories).Count());
    }

    [TestMethod]
    public async Task NetworkFailureDoesNotPublishOrLeavePartialArtifact()
    {
        using var client =
            new HttpClient(new Handler((_, _) => new(HttpStatusCode.ServiceUnavailable)));
        await Assert.ThrowsAsync<HttpRequestException>(
            () => new ArtifactAcquisition(temporary, client).AcquireAsync(Descriptor()));

        Assert.AreEqual(
            0, Directory.EnumerateFiles(temporary, "*", SearchOption.AllDirectories).Count());
    }

    [TestMethod]
    public async Task CancelledDownloadDisposesStreamAndRemovesPartialFile()
    {
        using var cancellation = new CancellationTokenSource();
        using var stream = new CancellingStream(cancellation);
        using var client = new HttpClient(
            new Handler((_, _) => new(HttpStatusCode.OK) { Content = new StreamContent(stream) }));
        await Assert.ThrowsAsync<OperationCanceledException>(
            () => new ArtifactAcquisition(temporary, client)
                      .AcquireAsync(Descriptor(), cancellation.Token));

        Assert.IsTrue(stream.WasRead);
        Assert.IsFalse(stream.CanRead);
        Assert.AreEqual(
            0, Directory.EnumerateFiles(temporary, "*", SearchOption.AllDirectories).Count());
    }

    [TestMethod]
    public async Task SimultaneousAcquisitionsPublishOneVerifiedFile()
    {
        using var client = new HttpClient(new Handler(
            (_, _) => new(HttpStatusCode.OK) { Content = new ByteArrayContent(Bytes) }));
        var first = new ArtifactAcquisition(temporary, client);
        var second = new ArtifactAcquisition(temporary, client);
        for (var attempt = 0; attempt < 20; attempt++)
        {
            var descriptor = Descriptor() with { FileName = $"concurrent-{attempt}.exe" };
            var results =
                await Task.WhenAll(first.AcquireAsync(descriptor), second.AcquireAsync(descriptor));
            Assert.AreEqual(results[0].Path, results[1].Path);
            CollectionAssert.AreEqual(Bytes, await File.ReadAllBytesAsync(results[0].Path));
        }

        Assert.AreEqual(
            20, Directory.EnumerateFiles(temporary, "*", SearchOption.AllDirectories).Count());
    }

    [TestMethod]
    [DataRow("../outside")]
    [DataRow("C:/outside.exe")]
    [DataRow("CON.exe")]
    [DataRow("name:stream")]
    [DataRow("trailing.")]
    public async Task InvalidFileNamesCannotEscapeCache(string fileName)
    {
        await Assert.ThrowsAsync<ArgumentException>(
            () => new ArtifactAcquisition(temporary).AcquireAsync(
                Descriptor() with { FileName = fileName }));

        Assert.IsFalse(Directory.Exists(temporary));
    }

    [TestMethod]
    [DataRow("http://example.invalid/file.exe")]
    [DataRow("https://user:secret@example.invalid/file.exe")]
    [DataRow("file://server/share/file.exe")]
    public async Task InsecureOrCredentialBearingArtifactSourceIsRejected(string source)
    {
        await Assert.ThrowsAsync<ArgumentException>(
            () => new ArtifactAcquisition(temporary).AcquireAsync(
                Descriptor() with { Source = new Uri(source) }));

        Assert.IsFalse(Directory.Exists(temporary));
    }

    private sealed class Handler(
        Func<HttpRequestMessage, CancellationToken, HttpResponseMessage> response)
        : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,
                                                               CancellationToken cancellationToken)
        {
            return Task.FromResult(response(request, cancellationToken));
        }
    }

    private sealed class CancellingStream(CancellationTokenSource cancellation) : MemoryStream
    (Bytes)
    {
        private bool wasRead;
        public bool WasRead => wasRead;

        public override ValueTask<int> ReadAsync(Memory<byte> buffer,
                                                 CancellationToken cancellationToken = default)
        {
            wasRead = true;
            cancellation.Cancel();
            return base.ReadAsync(buffer, cancellationToken);
        }
    }
}
