#ifndef EXPLOSION_H
#define EXPLOSION_H
#include <memory>

#include "GameMode.h"
#include "renderer.h"

class Explosion : public GameObject
{
public:
    // Explosion lasts 5 ticks before it dissappears
    float timer = .25f;
    Explosion() : GameObject("ot_Explosion", RS_Explosion) {}
    ~Explosion() noexcept override;
    void Update(PlayField& world) override
    {
        timer -= world.vsync.DeltaTime();
        if (timer <= 0)
        {
            world.RemoveObject(this);
        }
    }
};

Explosion::~Explosion() noexcept = default;
#endif // EXPLOSION_H
