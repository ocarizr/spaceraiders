#include "PlayField.h"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <stdexcept>
#include <memory>
#include <utility>
#include <vector>
#include <limits>
#include <type_traits>
#include <cmath>
#include <tuple>

#include <gtest/gtest.h>

#include "Alien.h"
#include "AlienLaser.h"
#include "BlockWall.h"
#include "GameMenus.h"
#include "GameMode.h"
#include "PlayerLaser.h"
#include "PlayerDiagonalLaser.h"
#include "PlayerShip.h"
#include "renderer.h"

std::default_random_engine rGen;

// Legacy consumers may observe counts, but only the world can mutate reservations.
static_assert(!std::is_assignable_v<decltype((std::declval<PlayField&>().PlayerLasers)), int>);
static_assert(!std::is_assignable_v<decltype((std::declval<PlayField&>().AlienLasers)), int>);

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
        return fire;
    }

    bool left = false;
    bool right = false;
    bool fire = false;
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
        world.SetAlienLaserCapacity(0);
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

namespace
{
class ProjectileScene : public testing::Test
{
  protected:
    void SetUp() override
    {
        rGen.seed(1);
        auto input = std::make_unique<ControlledInput>();
        controls = input.get();
        world.controllerInput = std::move(input);
        world.SetGameMode(new UntilPlayerDeath);
        world.AddPlayer(bounds);
        world.ProcessQueues();
    }

    int Effects()
    {
        return static_cast<int>(std::ranges::count_if(world.GameObjects(),
                                                      [](auto object)
                                                      {
                                                          return object->sprite == RS_Explosion;
                                                      }));
    }

    int PlayerShots()
    {
        return static_cast<int>(std::ranges::count_if(world.GameObjects(),
                                                      [](auto object)
                                                      {
                                                          return dynamic_cast<PlayerLaser*>(
                                                                     object) != nullptr;
                                                      }));
    }

    Vector2D bounds{80, 28};
    PlayField world{bounds};
    ControlledInput* controls = nullptr;
};

class ProjectileContacts : public ProjectileScene, public testing::WithParamInterface<int>
{
  protected:
    GameObjPtr Shot(Vector2D position = Vector2D(10, 10))
    {
        GameObjPtr laser = GetParam() == 0
                               ? static_cast<GameObjPtr>(new PlayerLaser)
                               : static_cast<GameObjPtr>(new PlayerDiagonalLaser(GetParam()));
        laser->pos = position;
        EXPECT_TRUE(world.SpawnLaser(laser));
        world.ProcessQueues();
        return laser;
    }
};

TEST_P(ProjectileContacts, OpposingLasersProduceOneImpactAndReturnBothSlots)
{
    auto playerShot = Shot();
    auto enemy = new AlienLaser;
    enemy->pos = playerShot->pos;
    ASSERT_TRUE(world.SpawnLaser(enemy));
    world.ProcessQueues();
    playerShot->Update(world);
    enemy->Update(world); // Already logically removed: cannot produce another effect.
    playerShot->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(world.AlienLasers, 10);
    EXPECT_EQ(Effects(), 1);
}

TEST_P(ProjectileContacts, EnemyCanResolveTheSameContactFirst)
{
    auto playerShot = Shot();
    auto enemy = new AlienLaser;
    enemy->pos = playerShot->pos;
    world.SpawnLaser(enemy);
    world.ProcessQueues();
    enemy->Update(world);
    playerShot->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(Effects(), 1);
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(world.AlienLasers, 10);
}

TEST_P(ProjectileContacts, OneLaserHitsOnlyOneOfTwoOverlappingAliens)
{
    auto shot = Shot();
    for (int index = 0; index < 2; ++index)
    {
        auto alien = new Alien;
        alien->pos = shot->pos;
        world.AddObject(alien);
    }

    world.ProcessQueues();
    shot->Update(world);
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienCount(), 1);
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(Effects(), 1);
    EXPECT_EQ(world.PlayerLasers, 5);
}

TEST_P(ProjectileContacts, WallTakesExactlyFiveSeparatePlayerHits)
{
    auto wall = new BlockWall;
    wall->pos = Vector2D(10, 10);
    world.AddObject(wall);
    world.ProcessQueues();
    for (int hit = 0; hit < 5; ++hit)
    {
        auto shot = Shot(wall->pos);
        shot->Update(world);
        shot->Update(world);
        EXPECT_EQ(world.IsLiveObject(wall), hit < 4);
        world.ProcessQueues();
        EXPECT_EQ(Effects(), hit + 1);
        EXPECT_EQ(world.PlayerLasers, 5);
    }

    EXPECT_EQ(world.GetGameMode()->GetScore(), 0);
}

TEST_P(ProjectileContacts, FriendlyLasersDoNotCollide)
{
    auto first = Shot();
    auto second = Shot();
    first->Update(world);
    second->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 2);
    EXPECT_EQ(Effects(), 0);
    EXPECT_EQ(world.PlayerLasers, 3);
}

TEST_P(ProjectileContacts, TopAndSideExitReturnSlotsWithoutImpact)
{
    auto shot = Shot(Vector2D(GetParam() > 0 ? 79.99f : 0.01f, 10));
    if (GetParam() == 0)
    {
        shot->pos.y = 0.01f;
    }

    shot->Update(world);
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(Effects(), 0);
}

TEST_P(ProjectileContacts, GenericRemovalAndPendingCancellationReleaseOneSlot)
{
    auto shot = Shot();
    EXPECT_TRUE(world.RemoveObject(shot));
    EXPECT_FALSE(world.RemoveObject(shot));
    world.DespawnLaser(shot);
    EXPECT_EQ(world.PlayerLasers, 5);
    world.ProcessQueues();
    GameObjPtr pending = GetParam() == 0
                             ? static_cast<GameObjPtr>(new PlayerLaser)
                             : static_cast<GameObjPtr>(new PlayerDiagonalLaser(GetParam()));
    world.SpawnLaser(pending);
    EXPECT_EQ(world.PlayerLasers, 4);
    world.RemoveObject(pending);
    world.ProcessQueues();
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(PlayerShots(), 0);
}

TEST_P(ProjectileContacts, MovementIntoAnotherCellResolvesAnImpact)
{
    const float startX = GetParam() < 0 ? 10.01f : GetParam() > 0 ? 10.99f : 10.f;
    const float targetX = GetParam() < 0 ? 9.99f : GetParam() > 0 ? 11.01f : 10.f;
    auto shot = Shot(Vector2D(startX, 10.01f));
    auto alien = new Alien;
    alien->pos = Vector2D(targetX, 9.99f);
    world.AddObject(alien);
    world.ProcessQueues();
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(Effects(), 1);
    EXPECT_EQ(world.PlayerLasers, 5);
}

TEST_P(ProjectileContacts, OutsideShotCannotHitTheClampedIntegerCell)
{
    auto shot = Shot(Vector2D(-0.01f, 10));
    auto alien = new Alien;
    alien->pos = Vector2D(0, 10);
    world.AddObject(alien);
    world.ProcessQueues();
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_TRUE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetScore(), 0);
    EXPECT_EQ(Effects(), 0);
    EXPECT_EQ(world.PlayerLasers, 5);
}

INSTANTIATE_TEST_SUITE_P(AllPlayerShots, ProjectileContacts, testing::Values(-1, 0, 1),
                         [](const testing::TestParamInfo<int>& info)
                         {
                             return info.param < 0 ? "Left" : info.param > 0 ? "Right" : "Straight";
                         });

TEST_F(ProjectileScene, RealTripleFireRequiresTheWholeVolleyAndReturnsEverySlot)
{
    auto player = static_cast<PlayerShip*>(world.GetPlayerObject());
    player->StartTripleLaser();
    world.SetPlayerLaserCapacity(2);
    controls->fire = true;
    for (int tick = 0; tick < 30; ++tick)
    {
        world.Update();
        world.ProcessQueues();
    }

    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.PlayerLasers, 2);
    world.SetPlayerLaserCapacity(3);
    world.Update();
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 3);
    EXPECT_EQ(world.PlayerLasers, 0);
    for (auto object : world.GameObjects())
    {
        if (dynamic_cast<PlayerLaser*>(object))
        {
            world.DespawnLaser(object);
        }
    }

    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.PlayerLasers, 3);
}

TEST_F(ProjectileScene, BatchRejectionLeavesNoPartialVolley)
{
    world.SetPlayerLaserCapacity(2);
    std::vector<std::unique_ptr<GameObject>> volley;
    volley.emplace_back(std::make_unique<PlayerLaser>());
    volley.emplace_back(std::make_unique<PlayerDiagonalLaser>(-1));
    volley.emplace_back(std::make_unique<PlayerDiagonalLaser>(1));
    EXPECT_FALSE(world.SpawnPlayerLasers(std::move(volley)));
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.PlayerLasers, 2);
    world.SetPlayerLaserCapacity(5);
    std::vector<std::unique_ptr<GameObject>> invalid;
    invalid.emplace_back(std::make_unique<PlayerLaser>());
    invalid.emplace_back(std::make_unique<AlienLaser>());
    EXPECT_FALSE(world.SpawnPlayerLasers(std::move(invalid)));
    EXPECT_FALSE(world.SpawnPlayerLasers({}));
    std::vector<std::unique_ptr<GameObject>> emptyShot;
    emptyShot.emplace_back(nullptr);
    EXPECT_FALSE(world.SpawnPlayerLasers(std::move(emptyShot)));
    EXPECT_FALSE(world.SpawnLaser(nullptr));
    EXPECT_FALSE(world.SpawnLaser(new Explosion));
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.PlayerLasers, 5);
}

TEST_F(ProjectileScene, PendingShotsSpendBudgetAndDirectAddCannotBypassIt)
{
    world.SetPlayerLaserCapacity(1);
    auto first = new PlayerDiagonalLaser(-1);
    ASSERT_TRUE(world.SpawnLaser(first));
    EXPECT_FALSE(world.SpawnLaser(first));
    EXPECT_FALSE(world.SpawnLaser(new PlayerLaser));
    EXPECT_FALSE(world.AddObject(new PlayerDiagonalLaser(1)));
    world.ProcessQueues();
    EXPECT_EQ(PlayerShots(), 1);
    EXPECT_EQ(world.PlayerLasers, 0);
}

TEST_F(ProjectileScene, ClearingResetsCapacityAndActiveAndPendingReservations)
{
    world.SetPlayerLaserCapacity(9);
    world.SetAlienLaserCapacity(4);
    world.SpawnLaser(new PlayerLaser);
    world.SpawnLaser(new AlienLaser);
    world.ProcessQueues();
    world.SpawnLaser(new PlayerDiagonalLaser(-1));
    world.SpawnLaser(new AlienLaser);
    world.RemoveAllObjects();
    world.ProcessQueues();
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 5);
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(world.AlienLasers, 10);
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
}

TEST_F(ProjectileScene, SmallerCapacityWaitsForExistingShotsToDrain)
{
    for (int shot = 0; shot < 3; ++shot)
    {
        world.SpawnLaser(new AlienLaser);
    }

    world.ProcessQueues();
    world.SetAlienLaserCapacity(1);
    EXPECT_EQ(world.AlienLasers, 0);
    EXPECT_FALSE(world.SpawnLaser(new AlienLaser));
    for (auto object : world.GameObjects())
    {
        if (dynamic_cast<AlienLaser*>(object))
        {
            world.RemoveObject(object);
            EXPECT_GE(world.AlienLasers.load(), 0);
        }
    }

    world.ProcessQueues();
    EXPECT_EQ(world.AlienLasers, 1);
    EXPECT_THROW(world.SetAlienLaserCapacity(-1), std::invalid_argument);
    EXPECT_THROW(world.SetPlayerLaserCapacity(-1), std::invalid_argument);
}

TEST_F(ProjectileScene, NewWaveAccountsForExistingEnemyShots)
{
    world.SpawnLaser(new AlienLaser);
    world.ProcessQueues();
    world.SpawnLaser(new AlienLaser);
    world.AddAlienWave(bounds);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienLasers.load() + world.AlienLaserCount(), world.AlienCount());
}

TEST_F(ProjectileScene, FireBoostExpandsCapacityWithoutForgettingExistingShots)
{
    world.SpawnLaser(new PlayerLaser);
    auto player = static_cast<PlayerShip*>(world.GetPlayerObject());
    player->UpdateFireDelay(0.5f, world);
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 7);
    EXPECT_EQ(world.PlayerLasers, 6);
    world.ProcessQueues();
    for (auto object : world.GameObjects())
    {
        if (dynamic_cast<PlayerLaser*>(object))
        {
            world.RemoveObject(object);
        }
    }

    EXPECT_EQ(world.PlayerLasers, 7);
}

TEST_F(ProjectileScene, EnemyShotHitsPlayerOnceAndReturnsItsSlot)
{
    auto enemy = new AlienLaser;
    enemy->pos = world.GetPlayerObject()->pos;
    world.SpawnLaser(enemy);
    world.ProcessQueues();
    enemy->Update(world);
    enemy->Update(world);
    world.ProcessQueues();
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.AlienLasers, 10);
    EXPECT_EQ(Effects(), 1);
}

TEST_F(ProjectileScene, EnemyShotIsAbsorbedByWallWithoutDamagingIt)
{
    auto wall = new BlockWall;
    wall->pos = Vector2D(10, 10);
    world.AddObject(wall);
    auto enemy = new AlienLaser;
    enemy->pos = wall->pos;
    world.SpawnLaser(enemy);
    world.ProcessQueues();
    enemy->Update(world);
    world.ProcessQueues();
    EXPECT_TRUE(world.IsLiveObject(wall));
    EXPECT_EQ(Effects(), 1);
    for (int hit = 0; hit < 5; ++hit)
    {
        EXPECT_EQ(wall->DecreaseHealth(world), hit == 4);
    }
}

TEST_F(ProjectileScene, OpposingShotInterceptsBeforeAColocatedPlayer)
{
    auto defender = new PlayerLaser;
    auto attacker = new AlienLaser;
    defender->pos = attacker->pos = world.GetPlayerObject()->pos;
    world.SpawnLaser(defender);
    world.SpawnLaser(attacker);
    world.ProcessQueues();
    attacker->Update(world);
    world.ProcessQueues();
    EXPECT_TRUE(world.HasPlayer());
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(Effects(), 1);
}

TEST_F(ProjectileScene, EnemyShotLeavingBottomReturnsSlotWithoutEffect)
{
    auto shot = new AlienLaser;
    shot->pos = Vector2D(10, 27.99f);
    world.SpawnLaser(shot);
    world.ProcessQueues();
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.AlienLasers, 10);
    EXPECT_EQ(Effects(), 0);
}

TEST_F(ProjectileScene, DenseWallOverlapUsesBoundedDisplacementInARealTick)
{
    world.SetAlienLaserCapacity(0);
    auto wall = new BlockWall;
    wall->pos = Vector2D(10, 1);
    world.AddObject(wall);
    for (int row = 1; row < 25; ++row)
    {
        auto alien = new Alien;
        alien->pos = Vector2D(10, static_cast<float>(row));
        world.AddObject(alien);
    }

    world.Update();
    world.ProcessQueues();
    EXPECT_TRUE(world.IsLiveObject(wall));
    EXPECT_FLOAT_EQ(wall->pos.y, 25);
    EXPECT_TRUE(world.HasPlayer());
}

TEST_F(ProjectileScene, WallExitsCanvasInsteadOfRecursingPastDenseAliens)
{
    auto wall = new BlockWall;
    wall->pos = Vector2D(10, 1);
    world.AddObject(wall);
    for (int row = 1; row < 28; ++row)
    {
        auto alien = new Alien;
        alien->pos = Vector2D(10, static_cast<float>(row));
        world.AddObject(alien);
    }

    world.ProcessQueues();
    wall->Update(world); // Direct boundary case; a normal tick ends on the bottom alien first.
    EXPECT_FALSE(world.IsLiveObject(wall));
    world.ProcessQueues();
    EXPECT_EQ(Effects(), 0);
}

TEST_F(ProjectileScene, DisplacedWallKillsPlayerWithOneExplosion)
{
    auto wall = new BlockWall;
    wall->pos = Vector2D(10, 26);
    world.GetPlayerObject()->pos = Vector2D(10, 27);
    auto alien = new Alien;
    alien->pos = wall->pos;
    world.AddObject(alien);
    world.AddObject(wall);
    world.ProcessQueues();
    wall->Update(world);
    wall->Update(world);
    EXPECT_FALSE(world.HasPlayer());
    EXPECT_FALSE(world.IsLiveObject(wall));
    world.ProcessQueues();
    EXPECT_EQ(Effects(), 1);
}

TEST_F(ProjectileScene, TerminalWorldRejectsNewShotsWithoutSpendingSlots)
{
    world.RemoveObject(world.GetPlayerObject());
    world.Update();
    ASSERT_TRUE(world.GetGameMode()->GameOver());
    EXPECT_FALSE(world.SpawnLaser(new PlayerLaser));
    EXPECT_FALSE(world.SpawnLaser(new AlienLaser));
    world.ProcessQueues();
    EXPECT_EQ(world.PlayerLasers, 5);
    EXPECT_EQ(world.AlienLasers, 10);
    EXPECT_EQ(PlayerShots(), 0);
    EXPECT_EQ(world.AlienLaserCount(), 0);
}
} // namespace

namespace
{
TEST_F(ProjectileScene, HeadOnShotsCrossingACellBoundaryCollideInEitherOrder)
{
    for (const bool playerFirst : {false, true})
    {
        SCOPED_TRACE(playerFirst);
        const int previousEffects = Effects();
        auto player = new PlayerLaser;
        auto enemy = new AlienLaser;
        player->pos = Vector2D(10, 10.01f);
        enemy->pos = Vector2D(10, 9.99f);
        world.SpawnLaser(player);
        world.SpawnLaser(enemy);
        world.ProcessQueues();
        if (playerFirst)
        {
            player->Update(world);
            enemy->Update(world);
        }
        else
        {
            enemy->Update(world);
            player->Update(world);
        }

        world.ProcessQueues();
        EXPECT_EQ(PlayerShots(), 0);
        EXPECT_EQ(world.AlienLaserCount(), 0);
        EXPECT_EQ(Effects(), previousEffects + 1);
    }
}

TEST_F(ProjectileScene, FireBoostSaturatesCapacityWithoutIntegerOverflow)
{
    const int maximum = (std::numeric_limits<int>::max)();
    world.SetPlayerLaserCapacity(maximum);
    auto player = static_cast<PlayerShip*>(world.GetPlayerObject());
    player->UpdateFireDelay(0.5f, world);
    EXPECT_EQ(world.GetPlayerLaserCapacity(), maximum);
    EXPECT_EQ(world.PlayerLasers, maximum);
}
} // namespace

namespace
{
class ScriptedRandom : public RandomSource
{
  public:
    float fallback = 0.5f;
    std::vector<float> samples;
    std::size_t cursor = 0;
    std::size_t calls = 0;

    void Set(std::initializer_list<float> values)
    {
        samples = values;
        cursor = 0;
    }

    float Unit() override
    {
        ++calls;
        return cursor < samples.size() ? samples[cursor++] : fallback;
    }
};

class EvolutionScene : public ProjectileScene
{
  protected:
    void SetUp() override
    {
        ProjectileScene::SetUp();
        world.SetAlienLaserCapacity(0);
        alien = new Alien(random);
        alien->pos = Vector2D(10, 1);
        world.AddObject(alien);
        world.ProcessQueues();
    }

    void TearDown() override
    {
        world.RemoveAllObjects(); // Destroy consumers before the injected source.
    }

    void Energize()
    {
        // Energy rises by 0.005 per tick; 0.5 rejects every evolution roll.
        for (int tick = 0; tick < 500; ++tick)
        {
            alien->Update(world);
        }
    }

    ScriptedRandom random;
    GameObjPtr alien = nullptr;
};

TEST_F(EvolutionScene, EnergyIsRequiredEvenWhenAnEvolutionRollWouldSucceed)
{
    random.fallback = 0.f;
    for (int tick = 0; tick < 500; ++tick)
    {
        alien->Update(world);
    }

    EXPECT_EQ(alien->sprite, RS_Alien);
    EXPECT_EQ(random.calls, 500u); // No chance roll before sufficient energy.
}

TEST_F(EvolutionScene, SufficientEnergyStillRequiresAStrictChanceRoll)
{
    Energize();
    ASSERT_EQ(alien->sprite, RS_Alien);
    random.Set({0.f, Alien::EvolutionChancePerTick});
    alien->Update(world);
    EXPECT_EQ(alien->sprite, RS_Alien);
    random.Set({0.f, std::nextafter(Alien::EvolutionChancePerTick, 0.f)});
    alien->Update(world);
    EXPECT_EQ(alien->sprite, RS_BetterAlien);
    const auto calls = random.calls;
    alien->Update(world);
    EXPECT_EQ(random.calls, calls); // No re-evolution or fire roll with zero slots.
}

TEST_F(EvolutionScene, CrossingTheEnergyThresholdEnablesTheFirstChanceRoll)
{
    random.fallback = 0.9999f;
    for (int tick = 0; tick < 200; ++tick)
    {
        alien->Update(world);
    }

    ASSERT_EQ(alien->sprite, RS_Alien);
    ASSERT_EQ(random.calls, 200u);
    random.Set({0.5f, 0.f}); // Cross energy 2, then pass the independent roll.
    alien->Update(world);
    EXPECT_EQ(alien->sprite, RS_BetterAlien);
    EXPECT_EQ(random.calls, 202u);
}

class EvolutionFiring : public EvolutionScene,
                        public testing::WithParamInterface<std::tuple<bool, float, bool>>
{
};

TEST_P(EvolutionFiring, NormalAndBetterAliensUseTheirOwnProbabilityBoundary)
{
    const auto [better, sample, fires] = GetParam();
    if (better)
    {
        Energize();
        random.Set({0.f, 0.f});
        alien->Update(world);
        ASSERT_EQ(alien->sprite, RS_BetterAlien);
        random.Set({sample});
    }
    else
    {
        random.Set({0.f, sample});
    }

    world.SetAlienLaserCapacity(1);
    alien->Update(world);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienLaserCount(), fires ? 1 : 0);
}

INSTANTIATE_TEST_SUITE_P(
    Thresholds, EvolutionFiring,
    testing::Values(std::tuple{false, 0.f, true}, std::tuple{false, 0.499f, true},
                    std::tuple{false, 0.5f, false}, std::tuple{true, 0.5f, true},
                    std::tuple{true, 0.749f, true}, std::tuple{true, 0.75f, false}));

TEST_F(EvolutionScene, ActualLaserKillCreatesTheSelectedDropAndScoresOnce)
{
    random.Set({0.f, 0.01f});
    auto shot = new PlayerLaser;
    shot->pos = alien->pos;
    world.SpawnLaser(shot);
    world.ProcessQueues();
    shot->Update(world);
    world.ProcessQueues();
    EXPECT_FALSE(world.HasAliens());
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(std::ranges::count_if(world.GameObjects(),
                                    [](auto object)
                                    {
                                        return dynamic_cast<SpeedBoost*>(object) != nullptr;
                                    }),
              1);
}

enum class DropKind
{
    None,
    Speed,
    Fire,
    Triple
};

class DropSelection : public testing::TestWithParam<std::tuple<float, float, DropKind>>
{
};

TEST_P(DropSelection, ExplicitWeightsAndDropChanceUseHalfOpenIntervals)
{
    const auto [dropSample, choiceSample, expected] = GetParam();
    ScriptedRandom random;
    random.Set({dropSample, choiceSample});
    std::unique_ptr<PowerUp> drop(PowerUp::CreateDrop(random));
    if (expected == DropKind::None)
    {
        EXPECT_EQ(drop, nullptr);
        EXPECT_EQ(random.calls, 1u);
        return;
    }

    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(random.calls, 2u);
    EXPECT_EQ(dynamic_cast<SpeedBoost*>(drop.get()) != nullptr, expected == DropKind::Speed);
    EXPECT_EQ(dynamic_cast<FireBoost*>(drop.get()) != nullptr, expected == DropKind::Fire);
    EXPECT_EQ(dynamic_cast<TripleFire*>(drop.get()) != nullptr, expected == DropKind::Triple);
}

INSTANTIATE_TEST_SUITE_P(
    Boundaries, DropSelection,
    testing::Values(std::tuple{0.1f, 0.f, DropKind::None}, std::tuple{0.999f, 0.f, DropKind::None},
                    std::tuple{0.f, 0.f, DropKind::Speed},
                    std::tuple{0.099f, std::nextafter(0.03f, 0.f), DropKind::Speed},
                    std::tuple{0.f, 0.03f, DropKind::Fire},
                    std::tuple{0.f, std::nextafter(0.06f, 0.f), DropKind::Fire},
                    std::tuple{0.f, 0.06f, DropKind::Triple},
                    std::tuple{0.f, 0.999f, DropKind::Triple}));

class TemporaryBuffs : public ProjectileScene
{
  protected:
    static constexpr int TicksBeforeExpiry =
        static_cast<int>((PlayerShip::BuffDuration.count() - 1) / VSync::SimulationStep.count());

    PlayerShip& Player()
    {
        return *dynamic_cast<PlayerShip*>(world.GetPlayerObject());
    }

    void Advance(int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            Player().Update(world);
            world.ProcessQueues();
        }
    }

    float MoveOnce()
    {
        Player().pos.x = 40.f;
        controls->right = true;
        Player().Update(world);
        controls->right = false;
        return Player().pos.x - 40.f;
    }

    int FireAndClear()
    {
        controls->fire = true;
        for (int tick = 0; tick < 100 && PlayerShots() == 0; ++tick)
        {
            Advance(1);
        }

        controls->fire = false;
        const int count = PlayerShots();
        for (auto object : world.GameObjects())
        {
            if (dynamic_cast<PlayerLaser*>(object))
            {
                world.RemoveObject(object);
            }
        }

        world.ProcessQueues();
        return count;
    }
};

TEST_F(TemporaryBuffs, SpeedExpiresAtTheFirstFixedTickAfterItsDuration)
{
    Player().UpdateMovementSpeed(PlayerShip::SpeedBoostMultiplier);
    Advance(TicksBeforeExpiry - 1);
    EXPECT_NEAR(MoveOnce(), 6.f * VSync::DeltaTime(), 0.00001f);
    EXPECT_NEAR(MoveOnce(), 4.f * VSync::DeltaTime(), 0.00001f);
}

TEST_F(TemporaryBuffs, RepeatedSpeedPickupRefreshesWithoutStacking)
{
    SpeedBoost* raw = new SpeedBoost;
    std::unique_ptr<PowerUp> pickup(raw);
    ASSERT_TRUE(pickup->ApplyPowerUp(&Player(), world));
    Advance(200);
    ASSERT_TRUE(pickup->ApplyPowerUp(&Player(), world));
    Advance(200);
    EXPECT_NEAR(MoveOnce(), 6.f * VSync::DeltaTime(), 0.00001f);
    Advance(TicksBeforeExpiry - 201);
    EXPECT_NEAR(MoveOnce(), 4.f * VSync::DeltaTime(), 0.00001f);
}

TEST_F(TemporaryBuffs, FireRenewalDoesNotAddSlotsAndExpiryKeepsInflightShots)
{
    Player().UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
    Advance(200);
    Player().UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 7);
    Advance(TicksBeforeExpiry);
    for (int shot = 0; shot < 7; ++shot)
    {
        world.SpawnLaser(new PlayerLaser);
    }

    world.ProcessQueues();
    Advance(1);
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 5);
    EXPECT_EQ(PlayerShots(), 7);
    EXPECT_EQ(world.PlayerLasers, 0);
    EXPECT_FALSE(world.SpawnLaser(new PlayerLaser));
    world.RemoveAllObjects();
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 5);
    EXPECT_EQ(world.PlayerLasers, 5);
}

TEST_F(TemporaryBuffs, TripleFireExpiresAndCanBeActivatedAgain)
{
    Player().StartTripleLaser();
    Advance(30);
    EXPECT_EQ(FireAndClear(), 3);
    Advance(TicksBeforeExpiry + 1);
    EXPECT_EQ(FireAndClear(), 1);
    Player().StartTripleLaser();
    EXPECT_EQ(FireAndClear(), 3);
}

TEST_F(TemporaryBuffs, IndependentlyRenewedBuffDoesNotExtendOtherBuffs)
{
    Player().StartTripleLaser();
    Player().UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
    Advance(200);
    Player().StartTripleLaser();
    Advance(TicksBeforeExpiry + 1 - 200);
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 5);
    EXPECT_EQ(FireAndClear(), 3);
}

TEST_F(TemporaryBuffs, CheckedPickupRejectsNullWrongTypeAndForeignPlayer)
{
    std::unique_ptr<PowerUp> pickup(new SpeedBoost);
    PlayerShip foreign;
    Explosion wrongType;
    EXPECT_FALSE(pickup->ApplyPowerUp(nullptr, world));
    EXPECT_FALSE(pickup->ApplyPowerUp(&wrongType, world));
    EXPECT_FALSE(pickup->ApplyPowerUp(&foreign, world));
    EXPECT_NEAR(MoveOnce(), 4.f * VSync::DeltaTime(), 0.00001f);
}

TEST_F(TemporaryBuffs, FallingPickupAppliesThroughTheActualWorldAndIsRemoved)
{
    auto pickup = new FireBoost;
    pickup->pos = Player().pos;
    world.AddObject(pickup);
    world.ProcessQueues();
    world.Update();
    world.ProcessQueues();
    EXPECT_EQ(world.GetPlayerLaserCapacity(), 7);
    EXPECT_EQ(std::ranges::count_if(world.GameObjects(),
                                    [](auto object)
                                    {
                                        return dynamic_cast<PowerUp*>(object) != nullptr;
                                    }),
              0);
}

class PickupApplication : public TemporaryBuffs, public testing::WithParamInterface<DropKind>
{
};

TEST_P(PickupApplication, FallingSpeedAndTriplePickupsUseCheckedNativeApplication)
{
    PowerUp* pickup = GetParam() == DropKind::Speed ? static_cast<PowerUp*>(new SpeedBoost)
                                                    : static_cast<PowerUp*>(new TripleFire);
    pickup->pos = Player().pos;
    world.AddObject(pickup);
    world.ProcessQueues();
    world.Update();
    world.ProcessQueues();
    EXPECT_EQ(std::ranges::count_if(world.GameObjects(),
                                    [](auto object)
                                    {
                                        return dynamic_cast<PowerUp*>(object) != nullptr;
                                    }),
              0);

    if (GetParam() == DropKind::Speed)
    {
        EXPECT_NEAR(MoveOnce(), 6.f * VSync::DeltaTime(), 0.00001f);
    }
    else
    {
        EXPECT_EQ(FireAndClear(), 3);
    }
}

INSTANTIATE_TEST_SUITE_P(ActualPickups, PickupApplication,
                         testing::Values(DropKind::Speed, DropKind::Triple));

TEST_F(TemporaryBuffs, SaturatedFireCapacityRestoresOnlySlotsActuallyGranted)
{
    const int maximum = (std::numeric_limits<int>::max)();
    for (int baseline : {maximum - 1, maximum})
    {
        world.SetPlayerLaserCapacity(baseline);
        Player().UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
        EXPECT_EQ(world.GetPlayerLaserCapacity(), maximum);
        Advance(TicksBeforeExpiry + 1);
        EXPECT_EQ(world.GetPlayerLaserCapacity(), baseline);
    }
}

TEST_F(TemporaryBuffs, InvalidMultipliersDoNotActivateBuffs)
{
    for (float value : {0.f, -1.f, 1.f, std::numeric_limits<float>::quiet_NaN()})
    {
        Player().UpdateMovementSpeed(value);
        Player().UpdateFireDelay(value, world);
    }

    EXPECT_EQ(world.GetPlayerLaserCapacity(), 5);
    EXPECT_NEAR(MoveOnce(), 4.f * VSync::DeltaTime(), 0.00001f);
}

TEST_F(TemporaryBuffs, ActualFiringCadenceReturnsToBaselineAfterExpiry)
{
    const auto nextVolley = [this]()
    {
        controls->fire = true;
        for (int ticks = 1; ticks <= 100; ++ticks)
        {
            Advance(1);
            if (PlayerShots() == 0)
            {
                continue;
            }

            for (auto object : world.GameObjects())
            {
                if (dynamic_cast<PlayerLaser*>(object))
                {
                    world.RemoveObject(object);
                }
            }

            world.ProcessQueues();
            controls->fire = false;
            return ticks;
        }

        ADD_FAILURE() << "No volley within 100 fixed ticks";
        controls->fire = false;
        return 100;
    };

    const int baseline = nextVolley();
    Player().UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
    EXPECT_LT(nextVolley(), baseline);
    Advance(TicksBeforeExpiry + 1);
    nextVolley(); // Start a fresh unboosted cooldown after an idle interval.
    EXPECT_EQ(nextVolley(), baseline);
}

TEST_F(ProjectileScene, RandomInputSamplesEachActionOnlyOncePerTick)
{
    ScriptedRandom random;
    random.Set({0.f, 0.9f, 0.9f});
    world.controllerInput = std::make_unique<RndInput>(random);
    world.GetPlayerObject()->pos.x = 40.f;
    world.GetPlayerObject()->Update(world);
    EXPECT_EQ(random.calls, 3u);
    EXPECT_NEAR(world.GetPlayerObject()->pos.x, 40.f - 4.f * VSync::DeltaTime(), 0.00001f);
    world.controllerInput.reset();
}

std::string ReplayLegacyGame(unsigned int seed)
{
    rGen.seed(seed);
    Vector2D bounds(80, 28);
    PlayField world(bounds);
    auto input = std::make_unique<ControlledInput>();
    auto controls = input.get();
    world.controllerInput = std::move(input);
    world.SetGameMode(new UntilPlayerDeath);
    world.AddPlayer(bounds);
    // A small real scene keeps Debug/ASan replay below the per-case timeout.
    for (int count = 0; count < 3; ++count)
    {
        auto alien = new Alien;
        alien->pos = Vector2D(floatRand(5.f, 70.f)(rGen), 1.f);
        world.AddObject(alien);
    }

    world.SetAlienLaserCapacity(3);
    world.ProcessQueues();
    std::ostringstream events;
    for (int tick = 0; tick < 800 && !world.GetGameMode()->GameOver(); ++tick)
    {
        controls->left = tick % 120 < 60;
        controls->right = !controls->left;
        controls->fire = true;
        world.Update();
        world.ProcessQueues();
        events << tick << ':' << world.GetGameMode()->GetScore() << ':' << world.PlayerLasers << ':'
               << world.AlienLasers << '\n';
        for (auto object : world.GameObjects())
        {
            events << object->m_objType << ':' << object->pos.x << ':' << object->pos.y << ':'
                   << static_cast<int>(object->sprite) << '\n';
        }
    }

    events << rGen;
    return events.str();
}

TEST(LegacyReplay, SameSeedAndInputsReproduceActualWorldEventsWithinThisBuild)
{
    const auto first = ReplayLegacyGame(1);
    EXPECT_EQ(first, ReplayLegacyGame(1));
    EXPECT_NE(first, ReplayLegacyGame(2));
}
} // namespace
