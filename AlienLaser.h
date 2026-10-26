#ifndef ALIENLASER_H
#define ALIENLASER_H
#include "Explosion.h"

class AlienLaser : public GameObject
{
  public:
    AlienLaser() : GameObject("ot_AlienLaser", RS_AlienLaser)
    {
    }

    ~AlienLaser() noexcept override = default;

    void Update(PlayField& world) override
    {
        if (world.ResolveLaserCollision(this))
        {
            return;
        }

        pos.y += PlayField::LaserSpeedCellsPerSecond * world.vsync.DeltaTime();
        world.ResolveLaserCollision(this);
    }
};

#endif // ALIENLASER_H
