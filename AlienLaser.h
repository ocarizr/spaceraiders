#ifndef ALIENLASER_H
#define ALIENLASER_H
#include "Explosion.h"

class AlienLaser : public GameObject
{
public:
    AlienLaser() : GameObject("ot_AlienLaser", RS_AlienLaser) {}
    ~AlienLaser() noexcept override;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y += 6.f * world.vsync.DeltaTime();
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

        if (deleted)
        {
            world.DespawnLaser(static_cast<GameObjPtr>(this));
        }
    }
};

AlienLaser::~AlienLaser() noexcept = default;

#endif // ALIENLASER_H
