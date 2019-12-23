#ifndef BLOCKWALL_H
#define BLOCKWALL_H
#include "Explosion.h"

class BlockWall : public GameObject
{
    int health = 5;
public:
    BlockWall() : GameObject("ot_BlockWall", RS_BlockWall) {}
    inline ~BlockWall() noexcept override = default;

    bool DecreaseHealth(PlayField& world) noexcept override
    {
        --health;
        return health == 0;
    }

    void Update(PlayField& world) override
    {
        for(const auto& obj : world.GameObjects())
        {
            if(pos.IntCmp(obj->pos))
            {
                if(obj->m_objType == "ot_AlienShip")
                {
                    pos.y += 1.f;
                    Update(world);
                    break;
                }
                else if(obj->m_objType == "ot_PlayerShip")
                {
                    auto no = new Explosion;
                    no->pos = pos;
                    world.AddObject(no);
                    world.RemoveObject(obj);
                    world.RemoveObject(static_cast<GameObjPtr>(this));
                }
            }
        }
    }
};

#endif // BLOCKWALL_H
