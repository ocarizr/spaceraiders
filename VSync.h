#ifndef VSYNC_H
#define VSYNC_H
#include <algorithm>
#include <chrono>

// Legacy fixed-step scheduler. Wall-clock pacing never changes gameplay delta.
class VSync
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    // Retain the legacy 16 ms gameplay step (62.5 ticks/s, not exact 60 Hz).
    static constexpr std::chrono::milliseconds SimulationStep{16};
    // Bound recovery after stalls to 80 ms per frame, avoiding an unbounded backlog.
    static constexpr int MaxCatchUpSteps = 5;

    explicit VSync(TimePoint now = Clock::now()) : previous(now)
    {
    }

    static constexpr float DeltaTime() noexcept
    {
        return std::chrono::duration<float>(SimulationStep).count();
    }

    void Reset(TimePoint now = Clock::now()) noexcept
    {
        previous = now;
        accumulated = Duration::zero();
    }

    int Advance(TimePoint now) noexcept
    {
        // Defensive handling for injected/regressing timestamps; steady_clock itself
        // is monotonic. Retain the last valid anchor rather than counting time twice.
        if (now < previous)
        {
            return 0;
        }

        const Duration elapsed = now - previous;
        previous = now;
        const Duration maximumElapsed = SimulationStep * MaxCatchUpSteps;
        accumulated += (std::min)(elapsed, maximumElapsed);
        const int ticks = static_cast<int>(accumulated / SimulationStep);
        accumulated -= SimulationStep * ticks;
        return ticks;
    }

    Duration TimeUntilNextTick(TimePoint now) const noexcept
    {
        const Duration elapsed = now > previous ? now - previous : Duration::zero();
        const Duration step = SimulationStep;
        // Subtract only after bounding elapsed, avoiding overflow on a long stall.
        const Duration remaining = step - accumulated;
        return elapsed >= remaining ? Duration::zero() : remaining - elapsed;
    }

  private:
    TimePoint previous;
    Duration accumulated = Duration::zero();
};

#endif // VSYNC_H
