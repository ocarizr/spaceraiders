#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include <algorithm>
#include <memory>
#include <limits>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include "PlayerDiagonalLaser.h"

class PlayerShip : public GameObject
{
    static constexpr int TripleShotLaserCount = 3;
    static constexpr int AdditionalLaserSlotsPerFireBoost = 2;
    static constexpr float BaseFireDelay = 0.4f;
    static constexpr float MinimumFireDelay = 0.1f;
    static constexpr float BaseMovementSpeed = 4.f;
    static constexpr float MaximumMovementSpeed = 8.f;
    float delayTime = BaseFireDelay;
    float fireDelay = delayTime;
    float movementSpeed = BaseMovementSpeed;
    bool tripleLaser = false;
    std::chrono::milliseconds fireBoostRemaining{};
    std::chrono::milliseconds speedBoostRemaining{};
    std::chrono::milliseconds tripleFireRemaining{};
    int grantedFireSlots = 0;

    static bool Expire(std::chrono::milliseconds& remaining) noexcept
    {
        if (remaining == std::chrono::milliseconds::zero())
        {
            return false;
        }

        remaining =
            (std::max)(std::chrono::milliseconds::zero(), remaining - VSync::SimulationStep);
        return remaining == std::chrono::milliseconds::zero();
    }

    void UpdateBuffs(PlayField& world)
    {
        if (Expire(speedBoostRemaining))
        {
            movementSpeed = BaseMovementSpeed;
        }

        if (Expire(fireBoostRemaining))
        {
            delayTime = BaseFireDelay;
            world.SetPlayerLaserCapacity(
                (std::max)(0, world.GetPlayerLaserCapacity() - grantedFireSlots));
            grantedFireSlots = 0;
        }

        if (Expire(tripleFireRemaining))
        {
            tripleLaser = false;
        }
    }

  public:
    // Five simulation seconds; a pickup refreshes its own timer without stacking.
    static constexpr std::chrono::milliseconds BuffDuration{std::chrono::seconds{5}};
    static constexpr float SpeedBoostMultiplier = 1.5f;
    static constexpr float FireBoostMultiplier = 0.5f;

    PlayerShip() : GameObject("ot_PlayerShip", RS_Player)
    {
    }

    inline ~PlayerShip() noexcept override = default;

    void UpdateFireDelay(float rate, PlayField& world) noexcept
    {
        if (!std::isfinite(rate) || rate <= 0.f || rate >= 1.f)
        {
            return;
        }

        delayTime = std::clamp(BaseFireDelay * rate, MinimumFireDelay, BaseFireDelay);
        fireDelay = (std::min)(fireDelay, delayTime);
        if (fireBoostRemaining == std::chrono::milliseconds::zero())
        {
            const int capacity = world.GetPlayerLaserCapacity();
            const int headroom = (std::numeric_limits<int>::max)() - capacity;
            grantedFireSlots = (std::min)(AdditionalLaserSlotsPerFireBoost, headroom);
            world.SetPlayerLaserCapacity(capacity + grantedFireSlots);
        }

        fireBoostRemaining = BuffDuration;
    }

    void UpdateMovementSpeed(float rate) noexcept
    {
        if (!std::isfinite(rate) || rate <= 1.f)
        {
            return;
        }

        movementSpeed = (std::min)(MaximumMovementSpeed, BaseMovementSpeed * rate);
        speedBoostRemaining = BuffDuration;
    }

    void StartTripleLaser() noexcept
    {
        tripleLaser = true;
        tripleFireRemaining = BuffDuration;
    }

    void Update(PlayField& world) override
    {
        UpdateBuffs(world);
        const bool left = world.controllerInput->Left();
        const bool right = world.controllerInput->Right();
        const bool fire = world.controllerInput->Fire();
        if (left && right)
        {
        } // Don't move
        else if (left)
        {
            pos.x -= movementSpeed * world.vsync.DeltaTime();
        }
        else if (right)
        {
            pos.x += movementSpeed * world.vsync.DeltaTime();
        }

        const float lastColumn = (std::max)(0.f, world.bounds.x - 1.f);
        pos.x = std::clamp(pos.x, 0.f, lastColumn);

        const int requiredSlots = tripleLaser ? TripleShotLaserCount : 1;
        fireDelay -= world.vsync.DeltaTime();
        if (fire && world.PlayerLasers >= requiredSlots && fireDelay <= 0.f)
        {
            // Spawn laser
            std::vector<std::unique_ptr<GameObject>> lasers;
            lasers.emplace_back(std::make_unique<PlayerLaser>());
            Vector2D laserPos(pos.x, pos.y - 1);
            lasers.back()->pos = laserPos;

            if (tripleLaser)
            {
                lasers.emplace_back(std::make_unique<PlayerDiagonalLaser>(-1));
                laserPos.x -= 1;
                lasers.back()->pos = laserPos;

                lasers.emplace_back(std::make_unique<PlayerDiagonalLaser>(1));
                laserPos.x += 2;
                lasers.back()->pos = laserPos;
            }

            if (world.SpawnPlayerLasers(std::move(lasers)))
            {
                fireDelay = delayTime;
            }
        }
    }
};

#endif // PLAYERSHIP_H
