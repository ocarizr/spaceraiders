#ifndef PLAYERLASER_H
#define PLAYERLASER_H
#include "Explosion.h"

class PlayerLaser : public GameObject
{
public:
    PlayerLaser() { m_objType = new char[64]; memcpy(m_objType, "ot_PlayerLaser", 15); sprite = RS_PlayerLaser; }
    ~PlayerLaser() override;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y -= 1.f;
        if (pos.y < 0)
        {
            deleted = true;
        }

        for (auto it : world.GameObjects())
        {
            if (strcmp(it->m_objType, "ot_AlienShip") == 0 && it->pos.IntCmp(pos))
            {
                deleted = true;
                //Spawn explosion, kill the alien that we hit
                //ToDo - add scoring
                GameObject& no = *(new Explosion);
                no.pos = pos;
                world.AddObject(&no);
                if (it->DecreaseHealth())
                    world.RemoveObject(it);
            }
        }

        if (deleted)
        {
            world.DespawnLaser(this);
        }
    }
};

PlayerLaser::~PlayerLaser(){ delete[] m_objType; }

#endif // PLAYERLASER_H
