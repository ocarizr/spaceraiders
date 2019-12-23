#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include "PlayerLaser.h"

class PlayerShip : public GameObject
{
    float delayTime = .4f;
    float fireDelay = delayTime;
    bool tripleLaser = false;
    float movementSpeed = 4.f;

public:
    PlayerShip() : GameObject("ot_PlayerShip", RS_Player) {}
    inline ~PlayerShip() noexcept override = default;

    void UpdateFireDelay(float rate) noexcept
    {
        if(delayTime > .1f)
            delayTime *= rate;
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
            if(!tripleLaser)
            {
                fireDelay = delayTime;
                //Spawn laser
                GameObjPtr newLaser = new PlayerLaser;
                Vector2D laserPos(pos.x, pos.y - 1);
                newLaser->pos = laserPos;
                world.SpawnLaser(newLaser);
            }
            else
            {
                // Shoot Triple Laser
            }
        }
        else
            fireDelay -= world.vsync.DeltaTime();
    }
};

#endif // PLAYERSHIP_H
