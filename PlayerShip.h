#ifndef PLAYERSHIP_H
#define PLAYERSHIP_H
#include "PlayerLaser.h"

class PlayerShip : public GameObject
{
public:
    PlayerShip() { m_objType = new char[64]; memcpy(m_objType, "ot_PlayerShip", 14); sprite = RS_Player; }
    ~PlayerShip() override;

    void Update(PlayField& world) override
    {
        if (world.controllerInput->Left())
            pos.x -= 1;
        else if (world.controllerInput->Right())
            pos.x += 1;

        if (world.controllerInput->Fire() && world.PlayerLasers > 0)
        {
            //Spawn laser
            GameObject& newLaser = *(new PlayerLaser);
            newLaser.pos = pos;
            world.SpawnLaser(&newLaser);
        }
    }
};

PlayerShip::~PlayerShip() { delete m_objType; }

#endif // PLAYERSHIP_H
