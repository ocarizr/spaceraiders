using System.Diagnostics;

namespace Raiders.Diagnostics;

public struct ProfileScope : IDisposable
{
#if RA_ENABLE_PROFILING
    private ScopeState? state;
#endif

    // The caller's symbol removes the entire call, including argument evaluation.
    [Conditional("RA_ENABLE_PROFILING")]
    public static void Begin(ref ProfileScope scope, TelemetrySession session, string category,
                             string name)
    {
#if RA_ENABLE_PROFILING
        ArgumentException.ThrowIfNullOrWhiteSpace(category);
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (category.Length > 128 || name.Length > 128 || scope.state is not null)
        {
            throw new ArgumentException("Invalid or already initialized profile scope.");
        }

        var context = session.Context;
        var start = TelemetrySession.ClockNowUs;
        if (session.Reserve())
        {
            scope.state = new ScopeState(session, category, name, context, start);
        }
#endif
    }

    public void Dispose()
    {
#if RA_ENABLE_PROFILING
        state?.Dispose();
#endif
    }

#if RA_ENABLE_PROFILING
    private sealed class ScopeState(TelemetrySession session, string category, string name,
                                    TelemetryContext context, long start)
    {
        private int disposed;
        private readonly int thread = Environment.CurrentManagedThreadId;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) == 0)
            {
                var duration = TelemetrySession.ClockNowUs - start;
                session.Complete(new ProfileEvent(category, name, start, duration, context),
                                 thread == Environment.CurrentManagedThreadId);
            }
        }
    }
#endif
}
