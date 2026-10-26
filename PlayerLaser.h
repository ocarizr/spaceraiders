#ifndef PLAYERLASER_H
#define PLAYERLASER_H
#include <string>
#include <utility>

#include "Explosion.h"

class PlayerLaser : public GameObject
{
  public:
    PlayerLaser() : GameObject("ot_PlayerLaser", RS_PlayerLaser)
    {
    }

    PlayerLaser(std::string objName, unsigned char s) : GameObject(std::move(objName), s)
    {
    }

    ~PlayerLaser() noexcept override = default;

    void Update(PlayField& world) override
    {
        if (world.ResolveLaserCollision(this))
        {
            return;
        }

        Move(world);
        world.ResolveLaserCollision(this);
    }

  protected:
    virtual void Move(PlayField& world)
    {
        pos.y -= PlayField::LaserSpeedCellsPerSecond * world.vsync.DeltaTime();
    }
};

#endif // PLAYERLASER_H
