#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include "PlayerLaser.h"

class PlayerShip : public GameObject
{
    const float delayTime = .3f;
    float fireDelay = delayTime;
public:
    PlayerShip() : GameObject("ot_PlayerShip", RS_Player) {}
    ~PlayerShip() noexcept override;

    void Update(PlayField& world) override
    {
        if(world.controllerInput->Left() && world.controllerInput->Right()) {} // Don't move
        else if (world.controllerInput->Left())
            pos.x -= 4 * world.vsync.DeltaTime();
        else if (world.controllerInput->Right())
            pos.x += 4 * world.vsync.DeltaTime();

        if (world.controllerInput->Fire() && world.PlayerLasers > 0 && fireDelay <= 0.f)
        {
            fireDelay = delayTime;
            //Spawn laser
            GameObjPtr newLaser = new PlayerLaser;
            newLaser->pos = pos;
            world.SpawnLaser(newLaser);
        }
        else
            fireDelay -= world.vsync.DeltaTime();
    }
};

PlayerShip::~PlayerShip() noexcept = default;

#endif // PLAYERSHIP_H
