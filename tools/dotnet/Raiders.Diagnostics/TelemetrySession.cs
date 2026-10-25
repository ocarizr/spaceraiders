using System.Diagnostics;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace Raiders.Diagnostics;

public sealed record
TelemetryContext([property : JsonPropertyName("session_id")] string SessionId,
                 [property : JsonPropertyName("process_instance_id")] string ProcessInstanceId,
                 [property : JsonPropertyName("thread_id")] string ThreadId,
                 [property : JsonPropertyName("clock_domain_id")] string ClockDomainId,
                 [property : JsonPropertyName("correlation_id")] string? CorrelationId = null,
                 [property : JsonPropertyName("asset_id")] string? AssetId = null,
                 [property : JsonPropertyName("entity_id")] string? EntityId = null,
                 [property : JsonPropertyName("widget_id")] string? WidgetId = null,
                 [property : JsonPropertyName("vm_instance_id")] string? VmInstanceId = null);

public sealed record ProfileEvent(string Category, string Name, long TimestampUs, long DurationUs,
                                  TelemetryContext Context);

public sealed record ProfileStats(int Capacity, int Buffered, int InFlight, long Completed,
                                  long Dropped, long Rejected, bool Accepting);

public sealed class TelemetrySession
{
    private static readonly long Origin = Stopwatch.GetTimestamp();
    private static readonly string Process = "process." + Guid.NewGuid().ToString("N");
    private static readonly string Clock = "clock." + Guid.NewGuid().ToString("N");
    private static long nextThread;
    private static readonly ThreadLocal<string> ThreadIdentity =
        new(() => "thread." + Interlocked.Increment(ref nextThread));
    private readonly AsyncLocal<TelemetryContext?> ambient = new();
    private readonly object gate = new();
    private readonly Queue<ProfileEvent> events = new();
    private readonly int capacity;
    private bool accepting = true;
    private int inFlight;
    private long completed, dropped, rejected;
    private static long eventId;

    public TelemetrySession(string sessionId, int profileCapacity = 1024)
    {
        ValidateId(sessionId);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(profileCapacity);
        SessionId = sessionId;
        capacity = profileCapacity;
    }

    public string SessionId { get; }
    public static bool ProfilingEnabled
    {
        get {
#if RA_ENABLE_PROFILING
            return true;
#else
            return false;
#endif
        }
    }
    public string ProcessInstanceId => Process;
    public string ClockDomainId => Clock;
    public static long ClockNowUs => Stopwatch.GetElapsedTime(Origin).Ticks / 10;

    public TelemetryContext Context =>
        (ambient.Value ?? new TelemetryContext(SessionId, Process, ThreadIdentity.Value!, Clock))
            with { ThreadId = ThreadIdentity.Value! };

    public IDisposable PushContext(string? correlationId = null, string? assetId = null,
                                   string? entityId = null, string? widgetId = null,
                                   string? vmInstanceId = null)
    {
        foreach (var id in new[] { correlationId, assetId, entityId, widgetId, vmInstanceId })
        {
            if (id is not null)
            {
                ValidateId(id);
            }
        }

        var previous = ambient.Value;
        var current = Context;
        ambient.Value = current with { CorrelationId = correlationId ?? current.CorrelationId,
                                       AssetId = assetId ?? current.AssetId,
                                       EntityId = entityId ?? current.EntityId,
                                       WidgetId = widgetId ?? current.WidgetId,
                                       VmInstanceId = vmInstanceId ?? current.VmInstanceId };
        return new ContextLease(this, previous);
    }

    internal static void ValidateId(string value)
    {
        if (value is null || !Regex.IsMatch(value, @"\A[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}\z"))
        {
            throw new ArgumentException("Invalid telemetry identity.", nameof(value));
        }
    }

    internal string NextEventId()
    {
        return "event." + Interlocked.Increment(ref eventId);
    }

    internal bool Reserve()
    {
        lock (gate)
        {
            if (!accepting)
            {
                rejected++;
                return false;
            }

            if (events.Count + inFlight == capacity)
            {
                dropped++;
                return false;
            }

            inFlight++;
            return true;
        }
    }

    internal void Complete(ProfileEvent item, bool sameThread)
    {
        lock (gate)
        {
            inFlight--;
            if (!sameThread)
            {
                rejected++;
                return;
            }

            events.Enqueue(item);
            completed++;
        }
    }

    public void Stop()
    {
        lock (gate)
        {
            accepting = false;
        }
    }

    public ProfileEvent[] Drain()
    {
        lock (gate)
        {
            var result = events.ToArray();
            events.Clear();
            return result;
        }
    }

    public ProfileStats Stats
    {
        get {
            lock (gate)
            {
                return new(ProfilingEnabled ? capacity : 0, events.Count, inFlight, completed,
                           dropped, rejected, ProfilingEnabled && accepting);
            }
        }
    }

    private sealed class ContextLease(TelemetrySession session, TelemetryContext? previous)
        : IDisposable
    {
        private bool disposed;

        public void Dispose()
        {
            if (!disposed)
            {
                session.ambient.Value = previous;
                disposed = true;
            }
        }
    }
}
