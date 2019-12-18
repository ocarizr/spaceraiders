#ifndef ALIEN_H
#define ALIEN_H
#include "AlienLaser.h"

enum class AlienState
{
    as_Normal,
    as_Better
};

class Alien : public GameObject
{
public:
    Alien() : state(AlienState::as_Normal) { m_objType = new char[64]; memcpy(m_objType, "ot_AlienShip", 13); sprite = RS_Alien; }
    ~Alien() override;

private:
    // Alien constants - these could move out into PlayField as configurable variables
    const float maxUpdateRate = 0.01f;
    const float transformEnergy = 1.f;

    // Variables dictating energy level for upgrade, direction of movement, and current speed
    float health = 1.f;
    float energy = 0.f;
    float direction = 1.f;
    float velocity = 0.5f;
    AlienState state;

    void Transform()
    {
        state = AlienState::as_Better;
        sprite = RS_BetterAlien;
        velocity *= 2.f;
    }
    bool DecreaseHealth() override { health -= 1.f; return health <= 0; }

    void Update(PlayField& world) override
    {
        pos.x += direction * velocity;
        // Border check
        if (pos.x < 0 || pos.x >= world.bounds.x - 1)
        {
            direction = -direction;
            pos.y += 1;
        }

        // Border check vertical:
        if (pos.y >= world.bounds.y - 1)
        {
            // kill player
            GameObject* player = world.GetPlayerObject();
            if (player && pos.IntCmp(player->pos))
            {
                //Spawn explosion
                GameObject& no = *(new Explosion);
                no.pos = pos;
                world.AddObject(&no);
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
            GameObject& newLaser = *(new AlienLaser);
            newLaser.pos = pos;
            world.SpawnLaser(&newLaser);
        }
    }
};

Alien::~Alien()
{ delete m_objType; }

#endif // ALIEN_H
