#ifndef POWERUP_H
#define POWERUP_H
#include "GameMode.h"
#include "renderer.h"

class PlayerShip;

class PowerUp : public GameObject
{
    static constexpr float FallingSpeedCellsPerSecond = 3.f;

  public:
    PowerUp() : GameObject("ot_PowerUp", RS_PowerUP)
    {
    }

    virtual ~PowerUp() noexcept override = default;

    static constexpr float DropChance = 0.1f;
    // Preserve the original conditional 3/3/94 balance explicitly, not equal thirds.
    static constexpr int SpeedWeight = 3;
    static constexpr int FireWeight = 3;
    static constexpr int TripleWeight = 94;
    static PowerUp* CreateDrop(RandomSource& random);
    bool ApplyPowerUp(GameObjPtr player, PlayField& world);

    void Update(PlayField& world) override
    {
        if (!world.IsLiveObject(this))
        {
            return;
        }

        pos.y += FallingSpeedCellsPerSecond * world.vsync.DeltaTime();
        if (pos.y >= world.bounds.y)
        {
            world.RemoveObject(this);
            return;
        }

        auto player = world.GetPlayerObject();
        if (player && pos.IntCmp(player->pos) && ApplyPowerUp(player, world))
        {
            world.RemoveObject(this);
        }
    }

  protected:
    virtual void ApplyTo(PlayerShip& player, PlayField& world) = 0;
};

class TripleFire : public PowerUp
{
    inline ~TripleFire() noexcept override = default;

    void ApplyTo(PlayerShip& player, PlayField& world) override;
};

class SpeedBoost : public PowerUp
{
    inline ~SpeedBoost() noexcept override = default;

    void ApplyTo(PlayerShip& player, PlayField& world) override;
};

class FireBoost : public PowerUp
{
    inline ~FireBoost() noexcept override = default;

    void ApplyTo(PlayerShip& player, PlayField& world) override;
};

#endif // POWERUP_H
