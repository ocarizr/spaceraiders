#ifndef ALIEN_H
#define ALIEN_H
#include "AlienLaser.h"

enum class AlienState : unsigned char
{
    as_Normal,
    as_Better
};

class Alien : public GameObject
{
public:
    Alien() : GameObject("ot_AlienShip", RS_Alien) { state = AlienState::as_Better; }
    ~Alien() noexcept override;

private:
    // Alien constants - these could move out into PlayField as configurable variables
    const float maxUpdateRate = 0.01f;
    const float transformEnergy = 1.f;

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
        health -= 1.f;
        bool isDead = health <= 0;
        if(isDead)
            world.GetGameMode()->UpdateScore(state == AlienState::as_Normal ? 10 : 20);
        return isDead;
    }

    void Update(PlayField& world) override
    {
        pos.x += direction * velocity * world.vsync.DeltaTime();
        // Border check
        if (pos.x < 0 || pos.x >= world.bounds.x - 1)
        {
            direction = -direction;
            pos.y += 1 * world.vsync.DeltaTime();
        }

        // Border check vertical:
        if (pos.y >= world.bounds.y - 1)
        {
            // kill player
            auto player = world.GetPlayerObject();
            if (player && pos.IntCmp(player->pos))
            {
                //Spawn explosion
                GameObjPtr no = new Explosion;
                no->pos = pos;
                world.AddObject(no);
                world.RemoveObject(player);
            }
        }

        // Transform into better Alien
        if (state != AlienState::as_Better)
        {
            floatRand updateRate(-maxUpdateRate, 2 * maxUpdateRate);
            energy += updateRate(rGen);
            if (energy >= transformEnergy)
                Transform();
        }

        floatRand fireRate(0, 1);
        if (fireRate(rGen) < 0.5 && world.AlienLasers > 0)
        {
            //Spawn laser
            GameObjPtr newLaser = new AlienLaser;
            newLaser->pos = pos;
            world.SpawnLaser(newLaser);
        }
    }
};

Alien::~Alien() noexcept = default;

#endif // ALIEN_H
