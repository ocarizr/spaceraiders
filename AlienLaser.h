#ifndef ALIENLASER_H
#define ALIENLASER_H
#include "Explosion.h"

class AlienLaser : public GameObject
{
public:
    AlienLaser() { m_objType = new char[64]; memcpy(m_objType, "ot_AlienLaser", 14); sprite = RS_AlienLaser; }
    ~AlienLaser() override;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y += 1.f;
        if (pos.y > world.bounds.y)
        {
            deleted = true;
        }
        GameObject* player = world.GetPlayerObject();
        if (player && pos.IntCmp(player->pos))
        {
            deleted = true;
            //Spawn explosion, kill player
            GameObject& no = *(new Explosion);
            no.pos = pos;
            world.AddObject(&no);
            world.RemoveObject(player);
        }

        if (deleted)
        {
            world.DespawnLaser(static_cast<GameObject*>(this));
        }
    }
};

AlienLaser::~AlienLaser()
{ delete[] m_objType; }

#endif // ALIENLASER_H
