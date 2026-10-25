using System.Text;
using System.Text.Json;

namespace Raiders.Diagnostics;

public enum LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off
}

// Synchronous caller-owned sink; never writes to stdout or logs its own failures.
public sealed class Log
{
    private readonly TelemetrySession session;
    private readonly TextWriter sink;
    private readonly LogLevel minimum;
    private readonly int maxRecords, maxRecordBytes;
    private readonly object gate = new();
    private long written, rejected, failures;
    private bool faulted;

    public Log(TelemetrySession session, TextWriter sink, LogLevel minimum = LogLevel.Info,
               int maxRecords = 10000, int maxRecordBytes = 16384)
    {
        ArgumentNullException.ThrowIfNull(session);
        ArgumentNullException.ThrowIfNull(sink);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxRecords);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(maxRecordBytes);
        if (!Enum.IsDefined(minimum))
        {
            throw new ArgumentOutOfRangeException(nameof(minimum));
        }

        this.session = session;
        this.sink = sink;
        this.minimum = minimum;
        this.maxRecords = maxRecords;
        this.maxRecordBytes = maxRecordBytes;
    }

    public (long Written, long Rejected, long Failures) Stats
    {
        get {
            lock (gate)
            {
                return (written, rejected, failures);
            }
        }
    }

    public bool Write(LogLevel level, string category, Func<string> message,
                      Func<IReadOnlyDictionary<string, object?>>? attributes = null)
    {
        lock (gate)
        {
            if (level < minimum || level >= LogLevel.Off || written >= maxRecords || faulted)
            {
                rejected++;
                return false;
            }

            try
            {
                ArgumentException.ThrowIfNullOrWhiteSpace(category);
                var timestamp = TelemetrySession.ClockNowUs;
                var context = session.Context;
                var text = message();
                ArgumentNullException.ThrowIfNull(text);
                var record = new {
                    schema_version = 1, event_id = session.NextEventId(), timestamp_us = timestamp,
                    context, severity = level.ToString().ToLowerInvariant(), category,
                    message = text, attributes = attributes?.Invoke() ?? new Dictionary < string,
                    object ?>() };
                var json = JsonSerializer.Serialize(record);
                if ((long)Encoding.UTF8.GetByteCount(json) +
                        Encoding.UTF8.GetByteCount(sink.NewLine) >
                    maxRecordBytes)
                {
                    rejected++;
                    return false;
                }

                using var document = JsonDocument.Parse(json);
                if (!NumbersAreSafe(document.RootElement))
                {
                    rejected++;
                    return false;
                }

                try
                {
                    sink.WriteLine(json);
                    sink.Flush();
                }
                catch (Exception)
                {
                    faulted = true;
                    failures++;
                    return false;
                }

                written++;
                return true;
            }
            catch (Exception)
            {
                failures++;
                return false;
            }
        }
    }

    private static bool NumbersAreSafe(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Number)
        {
            return value.TryGetDouble(out var number) && double.IsFinite(number) &&
                   Math.Abs(number) <= 9007199254740991d;
        }

        if (value.ValueKind == JsonValueKind.Array)
        {
            return value.EnumerateArray().All(NumbersAreSafe);
        }

        if (value.ValueKind == JsonValueKind.Object)
        {
            return value.EnumerateObject().All(property => NumbersAreSafe(property.Value));
        }

        return true;
    }
}
