#include "PlayField.h"

#include <functional>
#include <ranges>
#include <type_traits>

#include <gtest/gtest.h>

#include "Alien.h"
#include "GameMode.h"
#include "PlayerLaser.h"
#include "PlayerShip.h"
#include "renderer.h"

// The production executable defines the same RNG; this test target links the
// actual world/entities without its interactive entry point.
std::default_random_engine rGen;

static_assert(!std::is_copy_constructible_v<PlayField>);
static_assert(!std::is_copy_assignable_v<PlayField>);
static_assert(!std::is_move_constructible_v<PlayField>);
static_assert(!std::is_move_assignable_v<PlayField>);
static_assert(!std::is_copy_constructible_v<Renderer>);
static_assert(!std::is_copy_assignable_v<Renderer>);
static_assert(!std::is_move_constructible_v<Renderer>);
static_assert(!std::is_move_assignable_v<Renderer>);

namespace
{
// Instrumented unit objects count destruction/updates; production integration
// cases below use the actual player, alien, lasers, modes and spawn routines.
class TrackedObject : public GameObject
{
  public:
    explicit TrackedObject(int& destroyed)
        : GameObject("test.object", RS_Player), destroyed(destroyed)
    {
    }

    ~TrackedObject() override
    {
        ++destroyed;
    }

    void Update(PlayField& world) override
    {
        ++updates;
        if (onUpdate)
        {
            onUpdate(world);
        }
    }

    int updates = 0;
    std::function<void(PlayField&)> onUpdate;

  private:
    int& destroyed;
};

class TrackedMode : public GameMode
{
  public:
    explicit TrackedMode(int& destroyed) : destroyed(destroyed)
    {
    }

    ~TrackedMode() override
    {
        ++destroyed;
    }

    void Update(PlayField&) override
    {
    }

  private:
    int& destroyed;
};

class TrackedInput : public Input
{
  public:
    explicit TrackedInput(int& destroyed) : destroyed(destroyed)
    {
    }

    ~TrackedInput() override
    {
        ++destroyed;
    }

    bool Left() override
    {
        return false;
    }

    bool Right() override
    {
        return false;
    }

    bool Fire() override
    {
        return false;
    }

  private:
    int& destroyed;
};

class LegacyLifetime : public testing::Test
{
  protected:
    void SetUp() override
    {
        rGen.seed(1);
    }

    Vector2D bounds{80, 28};
};

TEST_F(LegacyLifetime, DestructionDrainsActivePendingAndRemovalQueues)
{
    int destroyed = 0;
    int modesDestroyed = 0;
    int inputsDestroyed = 0;
    {
        PlayField world(bounds);
        auto active = new TrackedObject(destroyed);
        world.AddObject(active);
        world.ProcessQueues();
        world.AddObject(new TrackedObject(destroyed));
        auto pending = new TrackedObject(destroyed);
        world.AddObject(pending);
        EXPECT_TRUE(world.RemoveObject(active));
        EXPECT_TRUE(world.RemoveObject(pending));
        world.SetGameMode(new TrackedMode(modesDestroyed));
        world.controllerInput = std::make_unique<TrackedInput>(inputsDestroyed);
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 3);
    EXPECT_EQ(modesDestroyed, 1);
    EXPECT_EQ(inputsDestroyed, 1);
}

TEST_F(LegacyLifetime, ClearingPendingQueuesDoesNotResurrectObjectsAndAllowsReuse)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto active = new TrackedObject(destroyed);
    world.AddObject(active);
    world.ProcessQueues();
    auto pending = new TrackedObject(destroyed);
    world.AddObject(pending);
    world.RemoveObject(active);
    world.RemoveObject(pending);
    world.RemoveAllObjects();
    world.RemoveAllObjects();
    world.ProcessQueues();
    EXPECT_EQ(destroyed, 2);
    EXPECT_TRUE(std::ranges::empty(world.GameObjects()));
    world.AddPlayer(bounds);
    world.ProcessQueues();
    EXPECT_TRUE(world.HasPlayer());
}

TEST_F(LegacyLifetime, DuplicateRegistrationAndRemovalDestroyExactlyOnce)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto object = new TrackedObject(destroyed);
    EXPECT_FALSE(world.AddObject(nullptr));
    EXPECT_TRUE(world.AddObject(object));
    EXPECT_FALSE(world.AddObject(object));
    world.ProcessQueues();
    EXPECT_FALSE(world.AddObject(object));
    EXPECT_TRUE(world.RemoveObject(object));
    EXPECT_FALSE(world.AddObject(object));
    EXPECT_FALSE(world.RemoveObject(object));
    EXPECT_FALSE(world.RemoveObject(nullptr));
    EXPECT_EQ(destroyed, 0);
    EXPECT_TRUE(std::ranges::empty(world.GameObjects()));
    world.ProcessQueues();
    world.ProcessQueues();
    EXPECT_EQ(destroyed, 1);
}

TEST_F(LegacyLifetime, RemovingBeforeActivationDestroysWithoutUpdating)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto object = new TrackedObject(destroyed);
    world.AddObject(object);
    EXPECT_TRUE(world.RemoveObject(object));
    EXPECT_FALSE(world.RemoveObject(object));
    world.ProcessQueues();
    world.Update();
    EXPECT_EQ(destroyed, 1);
    EXPECT_TRUE(std::ranges::empty(world.GameObjects()));
}

TEST_F(LegacyLifetime, ForeignObjectsAreNotDeletedOrDereferencedByRemoval)
{
    int destroyed = 0;
    TrackedObject foreign(destroyed);
    PlayField world(bounds);
    EXPECT_FALSE(world.RemoveObject(&foreign));
    world.DespawnLaser(&foreign);
    world.DespawnLaser(nullptr);
    world.ProcessQueues();
    EXPECT_EQ(destroyed, 0);
}

TEST_F(LegacyLifetime, RemovalDuringUpdateSkipsLaterObjectsWithoutInvalidatingIteration)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto first = new TrackedObject(destroyed);
    auto second = new TrackedObject(destroyed);
    auto last = new TrackedObject(destroyed);
    first->onUpdate = [second](PlayField& current)
    {
        current.RemoveObject(second);
    };

    world.AddObject(first);
    world.AddObject(second);
    world.AddObject(last);
    world.ProcessQueues();
    world.Update();
    EXPECT_EQ(first->updates, 1);
    EXPECT_EQ(second->updates, 0);
    EXPECT_EQ(last->updates, 1);
    EXPECT_EQ(destroyed, 0);
    world.ProcessQueues();
    EXPECT_EQ(destroyed, 1);
}

TEST_F(LegacyLifetime, SelfRemovalAndSpawningWaitForTheQueueBoundary)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto parent = new TrackedObject(destroyed);
    auto child = new TrackedObject(destroyed);
    parent->onUpdate = [parent, child](PlayField& current)
    {
        current.AddObject(child);
        current.RemoveObject(parent);
    };

    world.AddObject(parent);
    world.ProcessQueues();
    world.Update();
    EXPECT_EQ(child->updates, 0);
    EXPECT_EQ(destroyed, 0);
    world.ProcessQueues();
    EXPECT_EQ(destroyed, 1);
    world.Update();
    EXPECT_EQ(child->updates, 1);
}

TEST_F(LegacyLifetime, ModeReplacementSamePointerAndNullHaveSingleOwnership)
{
    int destroyed = 0;
    PlayField world(bounds);
    auto first = new TrackedMode(destroyed);
    world.SetGameMode(first);
    world.SetGameMode(first);
    EXPECT_EQ(destroyed, 0);
    world.SetGameMode(new TrackedMode(destroyed));
    EXPECT_EQ(destroyed, 1);
    world.SetGameMode(nullptr);
    world.SetGameMode(nullptr);
    world.Update();
    EXPECT_EQ(destroyed, 2);
    EXPECT_EQ(world.GetGameMode(), nullptr);
}

TEST_F(LegacyLifetime, InputReplacementReleasesPreviousController)
{
    int destroyed = 0;
    PlayField world(bounds);
    world.controllerInput = std::make_unique<TrackedInput>(destroyed);
    world.controllerInput = std::make_unique<TrackedInput>(destroyed);
    EXPECT_EQ(destroyed, 1);
    world.controllerInput.reset();
    EXPECT_EQ(destroyed, 2);
}

TEST_F(LegacyLifetime, RealPlayerRemovalIsVisibleToTheRealModeBeforeDeletion)
{
    PlayField world(bounds);
    world.SetGameMode(new UntilPlayerDeath);
    world.AddPlayer(bounds);
    world.ProcessQueues();
    ASSERT_TRUE(world.HasPlayer());
    world.RemoveObject(world.GetPlayerObject());
    EXPECT_FALSE(world.HasPlayer());
    world.Update();
    EXPECT_TRUE(world.GetGameMode()->GameOver());
    world.ProcessQueues();
}

TEST_F(LegacyLifetime, RealWaveSupportsModeReplacementAndPendingTeardown)
{
    PlayField world(bounds);
    world.SetGameMode(new DestroyAllAliens);
    world.AddAlienWave(bounds);
    world.AddPlayer(bounds);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienCount(), 50);
    EXPECT_TRUE(world.HasPlayer());
    world.SetGameMode(new TimeAttack);
    ASSERT_NE(dynamic_cast<TimeAttack*>(world.GetGameMode()), nullptr);
    EXPECT_EQ(world.GetGameMode()->GetScore(), 0);
    auto objects = world.GameObjects();
    auto alien = *std::ranges::find_if(objects,
                                       [](auto object)
                                       {
                                           return object->m_objType == "ot_AlienShip";
                                       });

    world.RemoveObject(alien);
    EXPECT_EQ(world.AlienCount(), 49);
    world.AddAlienWave(bounds);
    // Destructor must release the real active wave, pending wave and removal queue.
}

TEST_F(LegacyLifetime, RealPlayerLaserRepeatedSpawnAndDespawnReturnOneSlot)
{
    PlayField world(bounds);
    const int initialSlots = world.PlayerLasers;
    auto laser = new PlayerLaser;
    world.SpawnLaser(laser);
    world.SpawnLaser(laser);
    EXPECT_EQ(world.PlayerLasers, initialSlots - 1);
    world.ProcessQueues();
    world.DespawnLaser(laser);
    world.DespawnLaser(laser);
    EXPECT_EQ(world.PlayerLasers, initialSlots);
    world.ProcessQueues();
    EXPECT_TRUE(std::ranges::empty(world.GameObjects()));
}

TEST_F(LegacyLifetime, RealSpawnedSceneCanTickAndRestartWithoutInteractiveInput)
{
    PlayField world(bounds);
    world.controllerInput = std::make_unique<RndInput>();
    world.SetGameMode(new UntilPlayerDeath);
    world.AddAlienWave(bounds);
    world.AddPlayer(bounds);
    world.ProcessQueues();
    for (int tick = 0; tick < 3; ++tick)
    {
        world.Update();
        world.ProcessQueues();
    }

    EXPECT_TRUE(world.HasPlayer());
    EXPECT_TRUE(world.HasAliens());
    world.RemoveAllObjects();
    world.SetGameMode(new DestroyAllAliens);
    world.AddAlienWave(bounds);
    world.AddPlayer(bounds);
    world.ProcessQueues();
    EXPECT_TRUE(world.HasPlayer());
    EXPECT_EQ(world.AlienCount(), 50);
    EXPECT_FALSE(world.GetGameMode()->GameOver());
}

TEST_F(LegacyLifetime, RealAlienLaserPendingDespawnReturnsOneSlot)
{
    PlayField world(bounds);
    world.AddObject(new Alien);
    world.ProcessQueues();
    const int initialSlots = world.AlienLasers;
    auto laser = new AlienLaser;
    world.SpawnLaser(laser);
    world.DespawnLaser(laser);
    world.DespawnLaser(laser);
    EXPECT_EQ(world.AlienLasers, initialSlots);
    world.ProcessQueues();
    EXPECT_EQ(world.AlienLaserCount(), 0);
    EXPECT_EQ(world.AlienCount(), 1);
}

TEST_F(LegacyLifetime, RealAlienCannotAwardScoreOrDropsAgainAfterDeath)
{
    PlayField world(bounds);
    world.SetGameMode(new UntilPlayerDeath);
    GameObjPtr alien = new Alien;
    world.AddObject(alien);
    world.ProcessQueues();
    EXPECT_TRUE(alien->DecreaseHealth(world));
    const auto rngAfterDeath = rGen;
    EXPECT_FALSE(alien->DecreaseHealth(world));
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(rGen, rngAfterDeath);
    EXPECT_TRUE(world.RemoveObject(alien));
    EXPECT_FALSE(world.RemoveObject(alien));
    world.ProcessQueues();
}

TEST_F(LegacyLifetime, TwoRealLasersCannotScoreTheSameAlienTwiceInOneFrame)
{
    PlayField world(bounds);
    world.SetGameMode(new UntilPlayerDeath);
    auto alien = new Alien;
    alien->pos = Vector2D(10, 10);
    world.AddObject(alien);
    auto first = new PlayerLaser;
    auto second = new PlayerLaser;
    first->pos = second->pos = Vector2D(10, 10 + 4 * world.vsync.DeltaTime());
    world.SpawnLaser(first);
    world.SpawnLaser(second);
    world.ProcessQueues();
    first->Update(world);
    EXPECT_FALSE(world.HasAliens());
    second->Update(world);
    EXPECT_EQ(world.GetGameMode()->GetScore(), 10);
    EXPECT_EQ(world.PlayerLasers, 4);
    world.ProcessQueues();
}

TEST_F(LegacyLifetime, RealRendererCanReleaseItsOwnedBuffersWithoutDrawing)
{
    // Allocates actual renderer buffers without requiring a console/interactive UI.
    Renderer renderer(bounds);
}
} // namespace
