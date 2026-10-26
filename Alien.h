#ifndef ALIEN_H
#define ALIEN_H
#include <algorithm>

#include "AlienLaser.h"

enum class AlienState : unsigned char
{
    as_Normal,
    as_Better
};

class Alien : public GameObject
{
  public:
    Alien() : GameObject("ot_AlienShip", RS_Alien)
    {
        state = AlienState::as_Normal;
    }

    inline ~Alien() noexcept override = default;

    // World collision phases and the alien's own move share this rule. Direct
    // contact removes both participants even when a better alien has two hit points.
    bool ResolvePlayerContact(PlayField& world)
    {
        auto player = world.GetPlayerObject();
        if (!player || health <= 0)
        {
            return false;
        }

        const bool contact = pos.IntCmp(player->pos);
        if (!contact && pos.y < world.bounds.y - 1.f)
        {
            return false;
        }

        if (contact)
        {
            Die(world);
            world.RemoveObject(this);
        }
        else
        {
            // Breaching the bottom kills the player regardless of its column.
            // Keep this alien at its last position in the frozen final scene.
            pos.y = world.bounds.y - 1.f;
        }

        auto explosion = new Explosion;
        explosion->pos = player->pos;
        world.AddObject(explosion);
        world.RemoveObject(player);
        return true;
    }

  private:
    // Alien constants - these could move out into PlayField as configurable variables
    const float maxUpdateRate = 0.01f;
    const float transformEnergy = 2.f;

    // Variables dictating energy level for upgrade, direction of movement, and current speed
    float health = 1.f;
    float energy = 0.f;
    float direction = 1.f;
    float velocity = 2.f;
    AlienState state;

    void Transform() noexcept
    {
        state = AlienState::as_Better;
        sprite = RS_BetterAlien;
        health = 2.f;
        velocity *= 2.f;
    }

    bool DecreaseHealth(PlayField& world) noexcept override
    {
        if (health <= 0)
        {
            return false;
        }

        constexpr float DamagePerHit = 1.f;
        if (health > DamagePerHit)
        {
            health -= DamagePerHit;
            return false;
        }

        Die(world);
        return true;
    }

    void Die(PlayField& world)
    {
        if (health <= 0)
        {
            return;
        }

        health = 0;
        world.GetGameMode()->UpdateScore(state == AlienState::as_Normal ? 10 : 20);

        floatRand dropRate(0, 10);
        if (dropRate(rGen) <= 1.f)
        {
            auto puRate = dropRate(rGen);
            PowerUp* pu;
            if (puRate < .3f)
            {
                pu = new SpeedBoost;
            }
            else if (puRate < .6f)
            {
                pu = new FireBoost;
            }
            else
            {
                pu = new TripleFire;
            }

            Vector2D dropPos(pos.x, pos.y + 1);
            pu->pos = dropPos;
            world.AddObject(pu);
        }
    }

    void Update(PlayField& world) override
    {
        if (health <= 0 || ResolvePlayerContact(world))
        {
            return;
        }

        pos.x += direction * velocity * world.vsync.DeltaTime();
        const float lastColumn = (std::max)(0.f, world.bounds.x - 1.f);
        if ((direction < 0 && pos.x <= 0) || (direction > 0 && pos.x >= lastColumn))
        {
            direction = -direction;
            pos.y += 1;
        }

        pos.x = std::clamp(pos.x, 0.f, lastColumn);
        pos.y = (std::min)(pos.y, world.bounds.y - 1.f);

        if (ResolvePlayerContact(world))
        {
            return;
        }

        // Transform into better Alien
        if (state != AlienState::as_Better)
        {
            floatRand updateRate(0, maxUpdateRate);
            energy += updateRate(rGen);
            if (energy >= transformEnergy)
            {
                Transform();
            }
        }

        floatRand fireRate(0, 1);
        if (fireRate(rGen) < .5f && world.AlienLasers > 0)
        {
            // Spawn laser
            GameObjPtr newLaser = new AlienLaser;
            Vector2D laserPos(pos.x, pos.y + 1);
            newLaser->pos = laserPos;
            world.SpawnLaser(newLaser);
        }
    }
};

#endif // ALIEN_H
