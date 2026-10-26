#include "PlayField.h"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "Alien.h"
#include "GameMenus.h"
#include "GameMode.h"
#include "PlayerLaser.h"
#include "PlayerShip.h"
#include "renderer.h"

std::default_random_engine rGen;

namespace
{
class ControlledInput : public Input
{
  public:
    bool Left() override
    {
        return left;
    }

    bool Right() override
    {
        return right;
    }

    bool Fire() override
    {
        return false;
    }

    bool left = false;
    bool right = false;
};

enum class ModeKind
{
    Survival,
    Timed,
    ClearAliens
};

class LegacyRules : public testing::TestWithParam<ModeKind>
{
  protected:
    void SetUp() override
    {
        rGen.seed(1);
        auto input = std::make_unique<ControlledInput>();
        controls = input.get();
        world.controllerInput = std::move(input);
        switch (GetParam())
        {
        case ModeKind::Survival:
            world.SetGameMode(new UntilPlayerDeath);
            break;
        case ModeKind::Timed:
            world.SetGameMode(new TimeAttack);
            break;
        case ModeKind::ClearAliens:
            world.SetGameMode(new DestroyAllAliens);
            break;
        }

        world.AddPlayer(bounds);
        world.ProcessQueues();
        world.AlienLasers = 0;
    }

    GameObjPtr AddAlien(Vector2D position)
    {
        GameObjPtr alien = new Alien;
        alien->pos = position;
        world.AddObject(alien);
        world.ProcessQueues();
        return alien;
    }

    void Tick()
    {
        world.Update();
        world.ProcessQueues();
    }

    void TryNextWave()
    {
        if (!world.HasAliens())
        {
            world.AddAlienWave(bounds);
        }

        world.ProcessQueues();
    }

    Vector2D bounds{80, 28};
    PlayField world{bounds};
    ControlledInput* controls = nullptr;
};

TEST_P(LegacyRules, PlayerIsClampedAtBothHorizontalBorders)
{
    AddAlien(Vector2D(10, 1));
    auto player = world.GetPlayerObject();
    controls->right = true;
    player->pos.x = bounds.x - 1.01f;
    Tick();
    EXPECT_FLOAT_EQ(player->pos.x, bounds.x - 1);
    Tick();
    EXPECT_FLOAT_EQ(player->pos.x, bounds.x - 1);
    controls->right = false;
    controls->left = true;
    player->pos.x = 0.01f;
    Tick();
    EXPECT_FLOAT_EQ(player->pos.x, 0);
    Tick();
    EXPECT_FLOAT_EQ(player->pos.x, 0);
}

TEST_P(LegacyRules, SimultaneousDirectionsDoNotMoveThePlayer)
{
    AddAlien(Vector2D(10, 1));
    controls->left = controls->right = true;
    const float initialX = world.GetPlayerObject()->pos.x;
    Tick();
    EXPECT_FLOAT_EQ(world.GetPlayerObject()->pos.x, initialX);
}

TEST_P(LegacyRules, AlienBouncesAndDescendsOnceWithoutLeavingTheCanvas)
{
    auto alien = AddAlien(Vector2D(bounds.x - 1.01f, 2));
    Tick();
    EXPECT_FLOAT_EQ(alien->pos.x, bounds.x - 1);
    EXPECT_FLOAT_EQ(alien->pos.y, 3);
    Tick();
    EXPECT_LT(alien->pos.x, bounds.x - 1);
    EXPECT_FLOAT_EQ(alien->pos.y, 3);
    // It is now travelling left; put it just inside that border.
    alien->pos.x = 0.01f;
    Tick();
    EXPECT_FLOAT_EQ(alien->pos.x, 0);
    EXPECT_FLOAT_EQ(alien->pos.y, 4);
    Tick();
    EXPECT_GT(alien->pos.x, 0);
    EXPECT_FLOAT_EQ(alien->pos.y, 4);
}

TEST_P(LegacyRules, AlienAtBottomKillsPlayerInAnotherColumnAndFreezes)
{
    const Vector2D lastPlayerPosition = world.GetPlayerObject()->pos;
    auto alien = AddAlien(Vector2D(5, bounds.y - 1));
    Tick();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
    const Vector2D finalPosition = alien->pos;
    const auto finalRandom = rGen;
    world.Update();
    EXPECT_FLOAT_EQ(alien->pos.x, finalPosition.x);
    EXPECT_FLOAT_EQ(alien->pos.y, finalPosition.y);
    EXPECT_EQ(rGen, finalRandom);
    EXPECT_EQ(world.GetGameMode()->GetScore(), 0);
    auto objects = world.GameObjects();
    auto effect = std::ranges::find_if(objects,
                                       [](auto object)
                                       {
                                           return object->sprite == RS_Explosion;
                                       });

    ASSERT_NE(effect, objects.end());
    EXPECT_FLOAT_EQ((*effect)->pos.x, lastPlayerPosition.x);
    EXPECT_FLOAT_EQ((*effect)->pos.y, lastPlayerPosition.y);
}

TEST_P(LegacyRules, AlienCrossingBottomByDescentEndsTheMatch)
{
    auto alien = AddAlien(Vector2D(bounds.x - 1.01f, bounds.y - 2));
    Tick();
    EXPECT_FLOAT_EQ(alien->pos.y, bounds.y - 1);
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}

TEST_P(LegacyRules, ContactAboveTheBottomDestroysBothAndAwardsScoreOnce)
{
    auto player = world.GetPlayerObject();
    player->pos = Vector2D(20, 5);
    AddAlien(player->pos);
    Tick();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    world.Update();
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(std::ranges::count_if(world.GameObjects(),
                                    [](auto object)
                                    {
                                        return object->sprite == RS_Explosion;
                                    }),
              1);
}

TEST_P(LegacyRules, BetterAlienAlsoDiesOnContactRegardlessOfItsTwoHitPoints)
{
    auto alien = AddAlien(Vector2D(10, 1));
    constexpr int MaxEvolutionTicks = 2000;
    for (int tick = 0; tick < MaxEvolutionTicks && alien->sprite != RS_BetterAlien; ++tick)
    {
        alien->Update(world);
    }

    ASSERT_EQ(alien->sprite, RS_BetterAlien);
    world.GetPlayerObject()->pos = alien->pos;
    Tick();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetScore(), 20);
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}

TEST_P(LegacyRules, PlayerMovingIntoAnAlreadyUpdatedAlienStillCollides)
{
    auto player = world.GetPlayerObject();
    // Reinsert the player after the alien to exercise the opposite update order.
    world.RemoveObject(player);
    world.ProcessQueues();
    AddAlien(Vector2D(11.05f, 5));
    world.AddPlayer(bounds);
    world.ProcessQueues();
    world.GetPlayerObject()->pos = Vector2D(10.99f, 5);
    controls->right = true;
    Tick();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}

TEST_P(LegacyRules, InitialOverlapIsResolvedBeforeThePlayerCanMoveAway)
{
    world.GetPlayerObject()->pos = Vector2D(10.99f, 5);
    AddAlien(world.GetPlayerObject()->pos);
    controls->right = true;
    Tick();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}

TEST_P(LegacyRules, EndingOnLastAlienNeverQueuesAnotherWave)
{
    auto alien = AddAlien(Vector2D(5, 1));
    world.RemoveObject(alien);
    world.RemoveObject(world.GetPlayerObject());
    Tick();
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
    const auto finalRandom = rGen;
    TryNextWave();
    world.AddAlienWave(bounds); // The producer itself must reject terminal calls.
    world.ProcessQueues();
    EXPECT_TRUE(std::ranges::empty(world.GameObjects()));
    EXPECT_EQ(rGen, finalRandom);
}

TEST_P(LegacyRules, LiveContinuousModesRepopulateButClearModeWins)
{
    AddAlien(Vector2D(10, 5));
    auto laser = new PlayerLaser;
    laser->pos = Vector2D(10, 5 + 4 * world.vsync.DeltaTime());
    world.SpawnLaser(laser);
    Tick();
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    TryNextWave();
    if (GetParam() == ModeKind::ClearAliens)
    {
        EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Won);
        EXPECT_EQ(world.AlienCount(), 0);
    }
    else
    {
        EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Running);
        EXPECT_GE(world.AlienCount(), 15);
        EXPECT_LE(world.AlienCount(), 30);
    }
}

TEST_P(LegacyRules, TerminalOutcomeCannotBeReversedByLaterSceneChanges)
{
    AddAlien(Vector2D(5, 1));
    world.RemoveObject(world.GetPlayerObject());
    Tick();
    world.AddPlayer(bounds);
    world.ProcessQueues();
    world.GetGameMode()->Update(world);
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}

// Exercise actual menu composition/rasterization while preserving the process's
// output stream; console window/glyph appearance is not verified by this fixture.
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

TEST_P(LegacyRules, FinalFramePreservesTheLastPositionWithTheRealRenderer)
{
    auto alien = AddAlien(Vector2D(5, bounds.y - 1));
    Tick();
    RenderItemList firstFrame;
    GameMenus::GenerateGameOverFrame(firstFrame, world);
    ASSERT_FALSE(firstFrame.empty());
    EXPECT_EQ(firstFrame.front().sprite, RS_Alien);
    EXPECT_FLOAT_EQ(firstFrame.front().pos.x, alien->pos.x);
    EXPECT_FLOAT_EQ(firstFrame.front().pos.y, alien->pos.y);
    Renderer renderer(bounds);
    ConsoleCapture capture;
    renderer.Update(firstFrame, world);
    const auto frozenImage = capture.output.str();
    const auto rowStride = static_cast<std::size_t>(bounds.x) + 1; // Canvas width plus newline.
    const auto offset = static_cast<std::size_t>(bounds.y - 1) * rowStride + 5;
    ASSERT_GT(frozenImage.size(), offset);
    EXPECT_EQ(frozenImage[offset], RS_Alien);
    capture.output.str("");
    world.Update();
    TryNextWave();
    RenderItemList secondFrame;
    GameMenus::GenerateGameOverFrame(secondFrame, world);
    renderer.Update(secondFrame, world);
    EXPECT_EQ(capture.output.str(), frozenImage);
}

INSTANTIATE_TEST_SUITE_P(AllModes, LegacyRules,
                         testing::Values(ModeKind::Survival, ModeKind::Timed,
                                         ModeKind::ClearAliens),
                         [](const testing::TestParamInfo<ModeKind>& info)
                         {
                             switch (info.param)
                             {
                             case ModeKind::Survival:
                                 return "Survival";
                             case ModeKind::Timed:
                                 return "Timed";
                             case ModeKind::ClearAliens:
                                 return "ClearAliens";
                             }

                             return "Unknown";
                         });

TEST(LegacyModeRules, PendingInitialPopulationIsCommittedBeforeModeEvaluation)
{
    PlayField world(Vector2D(80, 28));
    world.controllerInput = std::make_unique<ControlledInput>();
    world.SetGameMode(new DestroyAllAliens);
    auto bounds = world.bounds;
    world.AddPlayer(bounds);
    world.AddAlienWave(bounds);
    world.Update();
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Running);
    EXPECT_TRUE(world.HasPlayer());
    EXPECT_TRUE(world.HasAliens());
}

TEST(LegacyModeRules, TimeoutWinsWithSurvivingPlayerAndNeverQueuesANewWave)
{
    PlayField world(Vector2D(80, 28));
    world.controllerInput = std::make_unique<ControlledInput>();
    world.SetGameMode(new TimeAttack);
    auto bounds = world.bounds;
    world.AddPlayer(bounds);
    world.ProcessQueues();
    for (int tick = 0; tick < 3688; ++tick)
    {
        world.Update();
    }

    EXPECT_TRUE(world.HasPlayer());
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Won);
    world.AddAlienWave(bounds);
    world.ProcessQueues();
    EXPECT_FALSE(world.HasAliens());
    RenderItemList frame;
    GameMenus::GenerateGameOverFrame(frame, world);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(frame.front().sprite, RS_Player);
    std::string text;
    for (const auto& item : frame)
    {
        if (item.sprite != RS_BackgroundTile)
        {
            text += static_cast<char>(item.sprite);
        }
    }

    EXPECT_NE(text.find("Youwonthegame!"), std::string::npos);
    EXPECT_EQ(text.find("YouDied!"), std::string::npos);
}

TEST(LegacyModeRules, DeathOnTimeoutTickHasPriorityOverVictory)
{
    PlayField world(Vector2D(80, 28));
    world.controllerInput = std::make_unique<ControlledInput>();
    world.SetGameMode(new TimeAttack);
    auto bounds = world.bounds;
    world.AddPlayer(bounds);
    world.ProcessQueues();
    for (int tick = 0; tick < 3687; ++tick)
    {
        world.Update();
    }

    world.RemoveObject(world.GetPlayerObject());
    world.Update();
    EXPECT_EQ(world.GetGameMode()->GetOutcome(), GameOutcome::Lost);
}
} // namespace
