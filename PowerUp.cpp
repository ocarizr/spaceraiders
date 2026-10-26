#include "PowerUp.h"

#include "PlayerShip.h"

PowerUp* PowerUp::CreateDrop(RandomSource& random)
{
    if (random.Unit() >= DropChance)
    {
        return nullptr;
    }

    constexpr int TotalWeight = SpeedWeight + FireWeight + TripleWeight;
    const float choice = random.Unit() * TotalWeight;
    if (choice < SpeedWeight)
    {
        return new SpeedBoost;
    }

    if (choice < SpeedWeight + FireWeight)
    {
        return new FireBoost;
    }

    return new TripleFire;
}

bool PowerUp::ApplyPowerUp(GameObjPtr player, PlayField& world)
{
    auto ship = dynamic_cast<PlayerShip*>(player);
    if (!ship || player != world.GetPlayerObject())
    {
        return false;
    }

    ApplyTo(*ship, world);
    return true;
}

void TripleFire::ApplyTo(PlayerShip& player, PlayField&)
{
    player.StartTripleLaser();
}

void SpeedBoost::ApplyTo(PlayerShip& player, PlayField&)
{
    player.UpdateMovementSpeed(PlayerShip::SpeedBoostMultiplier);
}

void FireBoost::ApplyTo(PlayerShip& player, PlayField& world)
{
    player.UpdateFireDelay(PlayerShip::FireBoostMultiplier, world);
}
