#ifndef POWERUP_H
#define POWERUP_H
#include "GameMode.h"
#include "renderer.h"

class PowerUp : public GameObject
{
public:
    PowerUp() : GameObject("ot_PowerUp", RS_PowerUP) {}
    virtual ~PowerUp() noexcept override = default;
    virtual void ApplyPowerUp(GameObjPtr player, PlayField& world) = 0;

    void Update(PlayField& world) override
    {
        bool deleted = false;
        pos.y += 3.f * world.vsync.DeltaTime();
        if (pos.y > world.bounds.y)
        {
            deleted = true;
        }

        auto player = world.GetPlayerObject();
        if (player && pos.IntCmp(player->pos))
        {
            ApplyPowerUp(player, world);
            deleted = true;
        }

        if (deleted)
        {
            world.RemoveObject(static_cast<GameObjPtr>(this));
        }
    }
};

class TripleFire : public PowerUp
{
    inline ~TripleFire() noexcept override = default;
    void ApplyPowerUp(GameObjPtr player, PlayField& world) override;
};

class SpeedBoost : public PowerUp
{
    inline ~SpeedBoost() noexcept override = default;
    void ApplyPowerUp(GameObjPtr player, PlayField& world) override;
};

class FireBoost : public PowerUp
{
    inline ~FireBoost() noexcept override = default;
    void ApplyPowerUp(GameObjPtr player, PlayField& world) override;
};

#endif // POWERUP_H
