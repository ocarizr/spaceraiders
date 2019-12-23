#ifndef EXPLOSION_H
#define EXPLOSION_H
#include <memory>

#include "PowerUp.h"

class Explosion : public GameObject
{
public:
    // Explosion lasts 5 ticks before it dissappears
    float timer = .25f;
    Explosion() : GameObject("ot_Explosion", RS_Explosion) {}
    inline ~Explosion() noexcept override = default;
    void Update(PlayField& world) override
    {
        timer -= world.vsync.DeltaTime();
        if (timer <= 0)
        {
            world.RemoveObject(this);
        }
    }
};
#endif // EXPLOSION_H
