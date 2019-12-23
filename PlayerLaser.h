#ifndef PLAYERLASER_H
#define PLAYERLASER_H
#include "Explosion.h"

class PlayerLaser : public GameObject
{
public:
    PlayerLaser() : GameObject("ot_PlayerLaser", RS_PlayerLaser) {}
    inline ~PlayerLaser() noexcept override = default;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y -= 6.f * world.vsync.DeltaTime();
        if (pos.y < 0)
        {
            deleted = true;
        }

        for (auto it : world.GameObjects())
        {
            if (it->m_objType == "ot_AlienShip" && it->pos.IntCmp(pos))
            {
                deleted = true;

                if (it->DecreaseHealth(world))
                {
                    GameObjPtr no = new Explosion;
                    no->pos = pos;
                    world.AddObject(no);
                    world.RemoveObject(it);
                }
            }
        }

        if (deleted)
        {
            world.DespawnLaser(this);
        }
    }
};

#endif // PLAYERLASER_H
