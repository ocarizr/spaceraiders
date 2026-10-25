using System.Text;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[assembly:Parallelize(Scope = ExecutionScope.MethodLevel)]

namespace Raiders.Diagnostics.Tests;

[TestClass]
public sealed class TraceTests
{
    [TestMethod]
    public async Task ContextFlowsThroughAsyncWorkAndRestoresNestedValues()
    {
        var session = new TelemetrySession("session.test");
        using (session.PushContext(correlationId: "command.1", assetId: "asset.1"))
        {
            using (session.PushContext(entityId: "player.1"))
            {
                var child = await Task.Run(() => session.Context);
                Assert.AreEqual("command.1", child.CorrelationId);
                Assert.AreEqual("player.1", child.EntityId);
                Assert.AreEqual(session.ProcessInstanceId, child.ProcessInstanceId);
                Assert.AreEqual(session.ClockDomainId, child.ClockDomainId);
            }

            Assert.IsNull(session.Context.EntityId);
            Assert.AreEqual("asset.1", session.Context.AssetId);
        }

        Assert.IsNull(session.Context.CorrelationId);
        Assert.IsNull(session.Context.AssetId);
    }

    [TestMethod]
    public void LogsUseSchemaEnvelopeAndEscapeText()
    {
        var session = new TelemetrySession("session.test");
        using var sink = new StringWriter();
        var log = new Log(session, sink);
        using var context =
            session.PushContext(correlationId: "command.1", widgetId: ("button." + "1"));
        var before = TelemetrySession.ClockNowUs;
        Assert.IsTrue(log.Write(LogLevel.Info, "editor", () => "quote\"\nç",
                               () => new Dictionary<string, object?> { ["cost.bytes"] = 42 }));

        using var record = JsonDocument.Parse(sink.ToString());
        var root = record.RootElement;
        Assert.AreEqual(1, root.GetProperty("schema_version").GetInt32());
        Assert.AreEqual("info", root.GetProperty("severity").GetString());
        Assert.AreEqual("quote\"\nç", root.GetProperty("message").GetString());
        Assert.AreEqual("session.test",
                        root.GetProperty("context").GetProperty("session_id").GetString());
        Assert.AreEqual("button.1",
                        root.GetProperty("context").GetProperty("widget_id").GetString());
        Assert.AreEqual(42, root.GetProperty("attributes").GetProperty("cost.bytes").GetInt32());
        Assert.IsTrue(root.GetProperty("timestamp_us").GetInt64() >= before);
    }

    [TestMethod]
    public void FilteredAndFullLogsDoNotConstructPayloads()
    {
        using var sink = new StringWriter();
        var log = new Log(new TelemetrySession("session.test"), sink, maxRecords: 1);
        var evaluations = 0;
        string Payload()
        {
            evaluations++;
            return "payload";
        }

        Assert.IsFalse(log.Write(LogLevel.Debug, "test", Payload, () => throw new Exception()));

        Assert.IsTrue(log.Write(LogLevel.Info, "test", Payload));
        Assert.IsFalse(log.Write(LogLevel.Error, "test", Payload));
        Assert.AreEqual(1, evaluations);
        Assert.AreEqual(2L, log.Stats.Rejected);
    }

    [TestMethod]
    public void SinkFailuresAndOversizedRecordsAreReportedWithoutRecursion()
    {
        var session = new TelemetrySession("session.test");
        var failed = new Log(session, new FailingWriter());
        Assert.IsFalse(failed.Write(LogLevel.Error, "test", () => "failure"));

        Assert.AreEqual(1L, failed.Stats.Failures);
        Assert.IsFalse(failed.Write(LogLevel.Error, "test", () => throw new Exception()));

        Assert.AreEqual(1L, failed.Stats.Failures);
        using var sink = new StringWriter();
        var bounded = new Log(session, sink, maxRecordBytes: 32);
        Assert.IsFalse(bounded.Write(LogLevel.Info, "test", () => new string('x', 128)));

        Assert.AreEqual("", sink.ToString());
        Assert.AreEqual(1L, bounded.Stats.Rejected);
    }

    [TestMethod]
    public void ConcurrentLogsHaveUniqueEventIds()
    {
        using var sink = new StringWriter();
        var log = new Log(new TelemetrySession("session.test"), sink);
        Parallel.For(0, 100,
                     _ => Assert.IsTrue(log.Write(LogLevel.Info, "worker", () => "record")));

        var ids = sink.ToString()
                      .Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries)
                      .Select(line => JsonSerializer.Deserialize<JsonElement>(line)
                                          .GetProperty("event_id")
                                          .GetString())
                      .ToHashSet();
        Assert.AreEqual(100, ids.Count);
        Assert.AreEqual(100L, log.Stats.Written);
    }

    [TestMethod]
    public void SessionsDoNotReuseProcessEventIdsAndUnsafeNumbersAreRejected()
    {
        using var sink = new StringWriter();
        var first = new Log(new TelemetrySession("session.first"), sink);
        var second = new Log(new TelemetrySession("session.second"), sink);
        Assert.IsTrue(first.Write(LogLevel.Info, "test", () => "first"));

        Assert.IsTrue(second.Write(LogLevel.Info, "test", () => "second"));

        var lines =
            sink.ToString().Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries);
        var firstId =
            JsonSerializer.Deserialize<JsonElement>(lines[0]).GetProperty("event_id").GetString();
        var secondId =
            JsonSerializer.Deserialize<JsonElement>(lines[1]).GetProperty("event_id").GetString();
        Assert.AreNotEqual(firstId, secondId);
        Assert.IsFalse(first.Write(LogLevel.Info, "test", () => "unsafe",
                                  () => new Dictionary<string, object?> { ["bytes"] = long.MaxValue }));

        Assert.AreEqual(1L, first.Stats.Rejected);
    }

    [TestMethod]
    public void ProfilingSymbolControlsArgumentEvaluationAndExceptionCleanup()
    {
        var session = new TelemetrySession("session.test");
        var evaluations = 0;
        string Name()
        {
            evaluations++;
            return "scope";
        }

        try
        {
            ProfileScope scope = default;
            ProfileScope.Begin(ref scope, session, "editor", Name());
            using (scope)
            {
                throw new InvalidOperationException("expected");
            }
        }
        catch (InvalidOperationException)
        {
        }

#if RA_ENABLE_PROFILING
        Assert.AreEqual(1, evaluations);
        Assert.AreEqual(1, session.Drain().Length);
        Assert.AreEqual(0, session.Stats.InFlight);
#else
        Assert.AreEqual(0, evaluations);
        Assert.AreEqual(0, session.Drain().Length);
        Assert.AreEqual(0L, session.Stats.Completed);
        Assert.IsFalse(TelemetrySession.ProfilingEnabled);
        Assert.AreEqual(0, session.Stats.Capacity);
        Assert.IsFalse(session.Stats.Accepting);
#endif
    }

#if RA_ENABLE_PROFILING
    [TestMethod]
    public void NestedScopesPreserveContextTimingAndIdempotentDisposal()
    {
        var session = new TelemetrySession("session.test");
        using var context = session.PushContext(entityId: "player.1");
        ProfileScope outer = default, inner = default;
        ProfileScope.Begin(ref outer, session, "editor", "outer");
        ProfileScope.Begin(ref inner, session, "editor", "inner");
        inner.Dispose();
        inner.Dispose();
        outer.Dispose();
        var events = session.Drain();
        Assert.AreEqual(2, events.Length);
        Assert.AreEqual("inner", events[0].Name);
        Assert.AreEqual("player.1", events[0].Context.EntityId);
        Assert.IsTrue(events[1].TimestampUs <= events[0].TimestampUs);
        Assert.IsTrue(events[0].TimestampUs + events[0].DurationUs <=
                      events[1].TimestampUs + events[1].DurationUs);
    }

    [TestMethod]
    public void CapacityReservesActiveScopesAndStopAllowsTheirCompletion()
    {
        var session = new TelemetrySession("session.test", 1);
        ProfileScope first = default, overflow = default, stopped = default;
        ProfileScope.Begin(ref first, session, "test", "first");
        ProfileScope.Begin(ref overflow, session, "test", "overflow");
        Assert.AreEqual(1L, session.Stats.Dropped);
        session.Stop();
        ProfileScope.Begin(ref stopped, session, "test", "stopped");
        first.Dispose();
        Assert.AreEqual(1L, session.Stats.Rejected);
        Assert.AreEqual(1, session.Drain().Length);
        Assert.AreEqual(0, session.Drain().Length);
    }

    [TestMethod]
    public void ConcurrentScopesAreBoundedAndWrongThreadDisposalIsRejected()
    {
        var session = new TelemetrySession("session.test", 256);
        Parallel.For(0, 100,
                     _ =>
                     {
                         ProfileScope scope = default;
                         ProfileScope.Begin(ref scope, session, "test", "worker");
                         scope.Dispose();
                     });

        Assert.AreEqual(100, session.Drain().Length);
        ProfileScope moved = default;
        ProfileScope.Begin(ref moved, session, "test", "wrong-thread");
        var thread = new Thread(() => moved.Dispose());
        thread.Start();
        thread.Join();
        Assert.AreEqual(1L, session.Stats.Rejected);
        Assert.AreEqual(0, session.Stats.InFlight);
    }
#endif

    [TestMethod]
    [DataRow("")]
    [DataRow("invalid id")]
    [DataRow("ç")]
    public void InvalidIdentitiesAreRejected(string id)
    {
        Assert.ThrowsExactly<ArgumentException>(() => new TelemetrySession(id));
    }

    private sealed class FailingWriter : TextWriter
    {
        public override Encoding Encoding => Encoding.UTF8;

        public override void WriteLine(string? value)
        {
            throw new IOException("test sink failure");
        }
    }
}
