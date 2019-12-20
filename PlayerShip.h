#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include "PlayerLaser.h"

class PlayerShip : public GameObject
{
public:
    PlayerShip() : GameObject("ot_PlayerShip", RS_Player) {}
    ~PlayerShip() noexcept override;

    void Update(PlayField& world) override
    {
        if (world.controllerInput->Left())
            pos.x -= 1;
        else if (world.controllerInput->Right())
            pos.x += 1;

        if (world.controllerInput->Fire() && world.PlayerLasers > 0)
        {
            //Spawn laser
            GameObjPtr newLaser = new PlayerLaser;
            newLaser->pos = pos;
            world.SpawnLaser(newLaser);
        }
    }
};

PlayerShip::~PlayerShip() noexcept = default;

#endif // PLAYERSHIP_H
