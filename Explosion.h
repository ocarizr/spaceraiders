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
    Explosion() { m_objType = new char[64];  memcpy(m_objType, "ot_Explosion", 13); sprite = RS_Explosion; }
    ~Explosion() override;
    void Update(PlayField& world) override
    {
        --timer;
        if (!timer)
        {
            world.RemoveObject(this);
            delete this;
        }
    }
};

Explosion::~Explosion() { delete[] m_objType; }
#endif // EXPLOSION_H
