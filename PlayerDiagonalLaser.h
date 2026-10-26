#ifndef PLAYERDIAGONALLASER_H
#define PLAYERDIAGONALLASER_H
#include "PlayerLaser.h"

class PlayerDiagonalLaser : public PlayerLaser
{
    int direction = 0;

  public:
    PlayerDiagonalLaser() = default;

    explicit PlayerDiagonalLaser(int dir)
        : PlayerLaser("ot_PlayerDiagonalLaser", dir > 0 ? RS_PlayerRightLaser : RS_PlayerLeftLaser),
          direction(dir)
    {
    }

    ~PlayerDiagonalLaser() noexcept override = default;

  protected:
    void Move(PlayField& world) override
    {
        PlayerLaser::Move(world);
        pos.x += PlayField::LaserSpeedCellsPerSecond * direction * world.vsync.DeltaTime();
    }
};

#endif // PLAYERDIAGONALLASER_H
