#include "VSync.h"

#include <array>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>

#include "GameMode.h"
#include "PlayerShip.h"
#include "renderer.h"

std::default_random_engine rGen;

namespace
{
using namespace std::chrono_literals;
static_assert(VSync::Clock::is_steady);

// Controlled input tests actual player movement without keyboard polling or RNG.
class RightInput : public Input
{
  public:
    bool Left() override
    {
        return false;
    }

    bool Right() override
    {
        return true;
    }

    bool Fire() override
    {
        return false;
    }
};

class LegacyTime : public testing::Test
{
  protected:
    void SetUp() override
    {
        world.controllerInput = std::make_unique<RightInput>();
        world.SetGameMode(new TimeAttack);
        world.AddPlayer(bounds);
        world.ProcessQueues();
        world.vsync.Reset(start);
    }

    void RunFrame(std::chrono::milliseconds elapsed)
    {
        now += elapsed;
        const int ticks = world.vsync.Advance(now);
        for (int tick = 0; tick < ticks && !world.GetGameMode()->GameOver(); ++tick)
        {
            world.Update();
            world.ProcessQueues();
            ++executedTicks;
        }
    }

    Vector2D bounds{80, 28};
    PlayField world{bounds};
    const VSync::TimePoint start{};
    VSync::TimePoint now = start;
    int executedTicks = 0;
};

TEST(VSyncTiming, SubstepFramesAccumulateUntilTheExactBoundary)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    EXPECT_EQ(pacing.Advance(start), 0);
    EXPECT_EQ(pacing.Advance(start + 5ms), 0);
    EXPECT_EQ(pacing.Advance(start + 15ms), 0);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 15ms), 1ms);
    EXPECT_EQ(pacing.Advance(start + 16ms), 1);
    EXPECT_EQ(pacing.Advance(start + 16ms), 0);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 16ms), 16ms);
}

TEST(VSyncTiming, FractionalMillisecondsAreRetained)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    EXPECT_EQ(pacing.Advance(start + 15999us), 0);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 15999us), 1us);
    EXPECT_EQ(pacing.Advance(start + 16000us), 1);
    EXPECT_EQ(pacing.Advance(start + 32500us), 1);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 32500us), 15500us);
}

TEST(VSyncTiming, SlowFramesProduceMultipleIdenticalSimulationSteps)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    EXPECT_EQ(pacing.Advance(start + 48ms), 3);
    EXPECT_FLOAT_EQ(pacing.DeltaTime(), 0.016f);
    EXPECT_EQ(pacing.Advance(start + 112ms), 4);
    EXPECT_FLOAT_EQ(pacing.DeltaTime(), 0.016f);
}

TEST(VSyncTiming, StallDropsExcessTimeAndDoesNotCreateAnUnboundedBacklog)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    EXPECT_EQ(pacing.Advance(start + 7ms), 0);
    EXPECT_EQ(pacing.Advance(start + 1h), VSync::MaxCatchUpSteps);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 1h), 9ms);
    EXPECT_EQ(pacing.Advance(start + 1h), 0);
    EXPECT_EQ(pacing.Advance(start + 1h + 9ms), 1);
}

TEST(VSyncTiming, SleepAccountsForUpdateAndRenderingWorkAndSaturatesAtZero)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    ASSERT_EQ(pacing.Advance(start + 20ms), 1);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 20ms), 12ms);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 25ms), 7ms);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 32ms), 0ms);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 33ms), 0ms);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 1h), 0ms);
    // Asking for pacing never consumes simulated time.
    EXPECT_EQ(pacing.Advance(start + 36ms), 1);
}

TEST(VSyncTiming, RegressingInjectedTimeCannotMoveTheAnchorBackwards)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    ASSERT_EQ(pacing.Advance(start + 20ms), 1);
    EXPECT_EQ(pacing.Advance(start + 10ms), 0);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 10ms), 12ms);
    EXPECT_EQ(pacing.Advance(start + 32ms), 1);
}

TEST(VSyncTiming, ResetExcludesMenuPauseAndOldFractionalTime)
{
    const VSync::TimePoint start{};
    VSync pacing(start);
    EXPECT_EQ(pacing.Advance(start + 15ms), 0);
    pacing.Reset(start + 1h);
    EXPECT_EQ(pacing.Advance(start + 1h), 0);
    EXPECT_EQ(pacing.TimeUntilNextTick(start + 1h), 16ms);
    EXPECT_EQ(pacing.Advance(start + 1h + 16ms), 1);
}

TEST_F(LegacyTime, RealPlayerAndTimerMatchAcrossFastAndSlowFrameSchedules)
{
    const float initialX = world.GetPlayerObject()->pos.x;
    for (int frame = 0; frame < 100; ++frame)
    {
        RunFrame(16ms);
    }

    const float fastX = world.GetPlayerObject()->pos.x;
    const int fastTimer = static_cast<TimeAttack*>(world.GetGameMode())->GetTimer();
    ASSERT_EQ(executedTicks, 100);
    EXPECT_NEAR(fastX - initialX, 6.4f, 0.001f);

    world.RemoveAllObjects();
    world.SetGameMode(new TimeAttack);
    world.AddPlayer(bounds);
    world.ProcessQueues();
    world.vsync.Reset(start);
    now = start;
    executedTicks = 0;
    // Controlled frame delays model rendering/pacing; 50 frames, 100 ticks.
    for (int frame = 0; frame < 50; ++frame)
    {
        RunFrame(32ms);
    }

    EXPECT_EQ(executedTicks, 100);
    EXPECT_FLOAT_EQ(world.GetPlayerObject()->pos.x, fastX);
    EXPECT_EQ(static_cast<TimeAttack*>(world.GetGameMode())->GetTimer(), fastTimer);
}

TEST_F(LegacyTime, IrregularFramesPreserveTheSameTickCountAndMovement)
{
    constexpr std::array delays{3ms, 7ms, 22ms, 1ms, 47ms};
    const float initialX = world.GetPlayerObject()->pos.x;
    for (const auto delay : delays)
    {
        RunFrame(delay);
    }

    EXPECT_EQ(executedTicks, 5);
    EXPECT_NEAR(world.GetPlayerObject()->pos.x - initialX, 0.32f, 0.0001f);
    EXPECT_EQ(world.vsync.TimeUntilNextTick(now), 16ms);
}

TEST_F(LegacyTime, TimeAttackEndsAtTheFirstTickReachingItsDuration)
{
    // 59,000 / 16 = 3,687.5: tick 3,688 is the first terminal tick.
    for (int tick = 0; tick < 3687; ++tick)
    {
        RunFrame(16ms);
    }

    ASSERT_FALSE(world.GetGameMode()->GameOver());
    EXPECT_EQ(static_cast<TimeAttack*>(world.GetGameMode())->GetTimer(), 0);
    RunFrame(16ms);
    EXPECT_EQ(executedTicks, 3688);
    EXPECT_TRUE(world.GetGameMode()->GameOver());
    RunFrame(80ms);
    EXPECT_EQ(executedTicks, 3688);
    EXPECT_EQ(static_cast<TimeAttack*>(world.GetGameMode())->GetTimer(), 0);
    for (int tick = 0; tick < 100; ++tick)
    {
        world.GetGameMode()->Update(world);
    }

    EXPECT_EQ(static_cast<TimeAttack*>(world.GetGameMode())->GetTimer(), 0);
}

TEST_F(LegacyTime, CatchUpBatchStopsAtTheTimeAttackTerminalTick)
{
    for (int tick = 0; tick < 3685; ++tick)
    {
        RunFrame(16ms);
    }

    RunFrame(80ms);
    EXPECT_TRUE(world.GetGameMode()->GameOver());
    EXPECT_EQ(executedTicks, 3688);
}

TEST_F(LegacyTime, LargeStallAdvancesRealGameplayOnlyByTheRecoveryBudget)
{
    const float initialX = world.GetPlayerObject()->pos.x;
    RunFrame(1h);
    EXPECT_EQ(executedTicks, VSync::MaxCatchUpSteps);
    EXPECT_NEAR(world.GetPlayerObject()->pos.x - initialX, 0.32f, 0.0001f);
    EXPECT_EQ(static_cast<TimeAttack*>(world.GetGameMode())->GetTimer(), 58);
    RunFrame(0ms);
    EXPECT_EQ(executedTicks, VSync::MaxCatchUpSteps);
}

TEST_F(LegacyTime, RemovingThePlayerStillEndsTimeAttack)
{
    world.RemoveObject(world.GetPlayerObject());
    RunFrame(16ms);
    EXPECT_TRUE(world.GetGameMode()->GameOver());
    EXPECT_EQ(executedTicks, 1);
}

// Restore the real output stream even if a renderer assertion throws.
class ConsoleCapture
{
  public:
    ConsoleCapture() : original(std::cout.rdbuf(output.rdbuf()))
    {
    }

    ~ConsoleCapture()
    {
        std::cout.rdbuf(original);
    }

    std::ostringstream output;

  private:
    std::streambuf* original;
};

TEST_F(LegacyTime, RealRendererAndMonotonicClockKeepPacingBounded)
{
    Renderer renderer(bounds);
    ConsoleCapture capture;
    world.vsync.Reset();
    RenderItemList scene;
    for (auto object : world.GameObjects())
    {
        scene.emplace_back(object->pos, object->sprite);
    }

    renderer.Update(scene, world);
    EXPECT_FALSE(capture.output.str().empty());
    const auto delay = world.vsync.TimeUntilNextTick(VSync::Clock::now());
    EXPECT_GE(delay, VSync::Duration::zero());
    EXPECT_LE(delay, VSync::SimulationStep);
    std::this_thread::sleep_for(delay);
    const int ticks = world.vsync.Advance(VSync::Clock::now());
    EXPECT_GE(ticks, 1);
    EXPECT_LE(ticks, VSync::MaxCatchUpSteps);
    EXPECT_FLOAT_EQ(world.vsync.DeltaTime(), 0.016f);
}
} // namespace
