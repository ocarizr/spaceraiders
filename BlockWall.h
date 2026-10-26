#ifndef BLOCKWALL_H
#define BLOCKWALL_H
#include <algorithm>

#include "Explosion.h"

class BlockWall : public GameObject
{
    int health = 5;

  public:
    BlockWall() : GameObject("ot_BlockWall", RS_BlockWall)
    {
    }

    ~BlockWall() noexcept override = default;

    bool DecreaseHealth(PlayField&) noexcept override
    {
        if (health <= 0)
        {
            return false;
        }

        --health;
        return health == 0;
    }

    void Update(PlayField& world) override
    {
        if (!world.IsLiveObject(this))
        {
            return;
        }

        // Each overlap advances by one row; never recurse or exceed canvas height.
        const int maximumDisplacements = (std::max)(0, static_cast<int>(world.bounds.y));
        for (int step = 0; step < maximumDisplacements; ++step)
        {
            if (pos.y < 0 || pos.y >= world.bounds.y)
            {
                break;
            }

            auto player = world.GetPlayerObject();
            if (player && pos.IntCmp(player->pos))
            {
                world.RemoveObject(this);
                world.RemoveObject(player);
                auto explosion = new Explosion;
                explosion->pos = pos;
                world.AddObject(explosion);
                return;
            }

            const bool blocked = std::ranges::any_of(
                world.GameObjects(),
                [this](auto object)
                {
                    return object->m_objType == "ot_AlienShip" && pos.IntCmp(object->pos);
                });

            if (!blocked)
            {
                return;
            }

            pos.y += 1.f;
        }

        world.RemoveObject(this);
    }
};

#endif // BLOCKWALL_H
