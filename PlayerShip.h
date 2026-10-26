#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include <algorithm>
#include <memory>
#include <limits>
#include <utility>
#include <vector>

#include "PlayerDiagonalLaser.h"

class PlayerShip : public GameObject
{
    static constexpr int TripleShotLaserCount = 3;
    static constexpr int AdditionalLaserSlotsPerFireBoost = 2;
    float delayTime = .4f;
    float fireDelay = delayTime;
    float movementSpeed = 4.f;
    bool tripleLaser = false;

  public:
    PlayerShip() : GameObject("ot_PlayerShip", RS_Player)
    {
    }

    inline ~PlayerShip() noexcept override = default;

    void UpdateFireDelay(float rate, PlayField& world) noexcept
    {
        if (delayTime > .1f)
        {
            delayTime *= rate;
            const int capacity = world.GetPlayerLaserCapacity();
            const int headroom = (std::numeric_limits<int>::max)() - capacity;
            const int extraSlots = (std::min)(AdditionalLaserSlotsPerFireBoost, headroom);
            world.SetPlayerLaserCapacity(capacity + extraSlots);
        }
    }

    void UpdateMovementSpeed(float rate) noexcept
    {
        if (movementSpeed < 8.f)
        {
            movementSpeed *= rate;
        }
    }

    void StartTripleLaser() noexcept
    {
        if (!tripleLaser)
        {
            tripleLaser = true;
        }
    }

    void Update(PlayField& world) override
    {
        if (world.controllerInput->Left() && world.controllerInput->Right())
        {
        } // Don't move
        else if (world.controllerInput->Left())
        {
            pos.x -= movementSpeed * world.vsync.DeltaTime();
        }
        else if (world.controllerInput->Right())
        {
            pos.x += movementSpeed * world.vsync.DeltaTime();
        }

        const float lastColumn = (std::max)(0.f, world.bounds.x - 1.f);
        pos.x = std::clamp(pos.x, 0.f, lastColumn);

        const int requiredSlots = tripleLaser ? TripleShotLaserCount : 1;
        if (world.controllerInput->Fire() && world.PlayerLasers >= requiredSlots &&
            fireDelay <= 0.f)
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
        else
        {
            fireDelay -= world.vsync.DeltaTime();
        }
    }
};

#endif // PLAYERSHIP_H
