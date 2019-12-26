#ifndef PLAYERTRIPLELASER_H
#define PLAYERTRIPLELASER_H
#include "PlayerLaser.h"

class PlayerDiagonalLaser : public PlayerLaser
{
    int direction = 0;
public:
    PlayerDiagonalLaser() : PlayerLaser() {}
    explicit PlayerDiagonalLaser(int dir) :
        PlayerLaser("ot_PlayerDiagonalLaser",
                    dir > 0 ? RS_PlayerRightLaser : RS_PlayerLeftLaser)
    {
        direction = dir;
    }
    inline ~PlayerDiagonalLaser() noexcept override = default;

    void Update(PlayField &world) override
    {
        bool deleted = false;
        pos.y -= 4.f * world.vsync.DeltaTime();
        pos.x += 4.f * direction * world.vsync.DeltaTime();
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
#endif // PLAYERTRIPLELASER_H
