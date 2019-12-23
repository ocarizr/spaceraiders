#ifndef ALIENLASER_H
#define ALIENLASER_H
#include "Explosion.h"

class AlienLaser : public GameObject
{
public:
    AlienLaser() : GameObject("ot_AlienLaser", RS_AlienLaser) {}
    inline ~AlienLaser() noexcept override = default;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y += 4.f * world.vsync.DeltaTime();
        if (pos.y > world.bounds.y)
        {
            deleted = true;
        }
        auto player = world.GetPlayerObject();
        if (player && pos.IntCmp(player->pos))
        {
            deleted = true;
            //Spawn explosion, kill player
            GameObjPtr no = new Explosion;
            no->pos = pos;
            world.AddObject(no);
            world.RemoveObject(player);
        }

        for(const auto& obj : world.GameObjects())
        {
            if(obj->m_objType == "ot_BlockWall" && obj->pos.IntCmp(pos))
            {
                deleted = true;
                break;
            }
        }

        if (deleted)
        {
            world.DespawnLaser(static_cast<GameObjPtr>(this));
        }
    }
};

#endif // ALIENLASER_H
