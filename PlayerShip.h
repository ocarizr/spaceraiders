#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include "PlayerDiagonalLaser.h"

class PlayerShip : public GameObject
{
    float delayTime = .4f;
    float fireDelay = delayTime;
    float movementSpeed = 4.f;
    bool tripleLaser = false;

public:
    PlayerShip() : GameObject("ot_PlayerShip", RS_Player) {}
    inline ~PlayerShip() noexcept override = default;

    void UpdateFireDelay(float rate, PlayField& world) noexcept
    {
        if(delayTime > .1f)
        {
            delayTime *= rate;
            world.PlayerLasers += 2;
        }
    }

    void UpdateMovementSpeed(float rate) noexcept
    {
        if(movementSpeed < 8.f)
            movementSpeed *= rate;
    }

    void StartTripleLaser() noexcept
    {
        if(!tripleLaser)
            tripleLaser = true;

    }

    void Update(PlayField& world) override
    {
        if(world.controllerInput->Left() && world.controllerInput->Right()) {} // Don't move
        else if (world.controllerInput->Left())
            pos.x -= movementSpeed * world.vsync.DeltaTime();
        else if (world.controllerInput->Right())
            pos.x += movementSpeed * world.vsync.DeltaTime();

        if (world.controllerInput->Fire() && world.PlayerLasers > 0 && fireDelay <= 0.f)
        {
            fireDelay = delayTime;
            //Spawn laser
            std::list<GameObjPtr> lasers;
            lasers.emplace_back(new PlayerLaser);
            Vector2D laserPos(pos.x, pos.y - 1);
            lasers.back()->pos = laserPos;

            if(tripleLaser)
            {
                lasers.emplace_back(new PlayerDiagonalLaser(-1));
                laserPos.x -= 1;
                lasers.back()->pos = laserPos;

                lasers.emplace_back(new PlayerDiagonalLaser(1));
                laserPos.x += 2;
                lasers.back()->pos = laserPos;
            }

            for(const auto& laser : lasers)
                world.SpawnLaser(laser);
        }
        else
            fireDelay -= world.vsync.DeltaTime();
    }
};

#endif // PLAYERSHIP_H
