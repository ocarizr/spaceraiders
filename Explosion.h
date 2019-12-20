#ifndef EXPLOSION_H
#define EXPLOSION_H
#include <memory>

#include "PlayField.h"
#include "Renderer.h"

class Explosion : public GameObject
{
public:
    // Explosion lasts 5 ticks before it dissappears
    int timer = 5;
    Explosion() : GameObject("ot_Explosion", RS_Explosion) {}
    ~Explosion() noexcept override;
    void Update(PlayField& world) override
    {
        --timer;
        if (!timer)
        {
            world.RemoveObject(this);
        }
    }
};

Explosion::~Explosion() noexcept = default;
#endif // EXPLOSION_H
