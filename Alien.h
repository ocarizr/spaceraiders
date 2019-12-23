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
    Alien() : GameObject("ot_AlienShip", RS_Alien) { state = AlienState::as_Normal; }
    inline ~Alien() noexcept override = default;

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
        health -= 1.f;
        bool isDead = health <= 0;
        if(isDead)
        {
            world.GetGameMode()->UpdateScore(state == AlienState::as_Normal ? 10 : 20);

            floatRand dropRate(0, 10);
            if(dropRate(rGen) <= 1.f)
            {
                auto puRate = dropRate(rGen);
                PowerUp* pu;
                if(puRate < .3f)
                {
                    pu = new SpeedBoost;
                }
                else if(puRate < .6f)
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
        return isDead;
    }

    void Update(PlayField& world) override
    {
        pos.x += direction * velocity * world.vsync.DeltaTime();
        // Border check
        if (pos.x <= 0 || pos.x >= world.bounds.x - 1)
        {
            direction = -direction;
            pos.y += 1;
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
                DecreaseHealth(world);
            }
        }

        // Transform into better Alien
        if (state != AlienState::as_Better)
        {
            floatRand updateRate(0, maxUpdateRate);
            energy += updateRate(rGen);
            if (energy >= transformEnergy)
                Transform();
        }

        floatRand fireRate(0, 1);
        if (fireRate(rGen) < .5f && world.AlienLasers > 0)
        {
            //Spawn laser
            GameObjPtr newLaser = new AlienLaser;
            Vector2D laserPos(pos.x, pos.y + 1);
            newLaser->pos = laserPos;
            world.SpawnLaser(newLaser);
        }
    }
};

#endif // ALIEN_H
