#ifndef PLAYERLASER_H
#define PLAYERLASER_H
#include "Explosion.h"

class PlayerLaser : public GameObject
{
public:
    PlayerLaser() : GameObject("ot_PlayerLaser", RS_PlayerLaser) {}
    PlayerLaser(std::string objName, unsigned char s) : GameObject(objName, s) {}
    inline virtual ~PlayerLaser() noexcept override = default;

    virtual void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y -= 4.f * world.vsync.DeltaTime();
        if (pos.y < 0)
        {
            deleted = true;
        }

        for (auto it : world.GameObjects())
        {
            if ((it->m_objType == "ot_AlienShip" || it->m_objType == "ot_BlockWall")
                && it->pos.IntCmp(pos))
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
