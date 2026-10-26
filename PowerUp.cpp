#include "PowerUp.h"

#include "PlayerShip.h"

void TripleFire::ApplyPowerUp(GameObjPtr player, PlayField& world)
{
    auto p = reinterpret_cast<PlayerShip*>(player);
    p->StartTripleLaser();
}

void SpeedBoost::ApplyPowerUp(GameObjPtr player, PlayField& world)
{
    auto p = reinterpret_cast<PlayerShip*>(player);
    p->UpdateMovementSpeed(1.5f);
}

void FireBoost::ApplyPowerUp(GameObjPtr player, PlayField& world)
{
    auto p = reinterpret_cast<PlayerShip*>(player);
    p->UpdateFireDelay(.5f, world);
}
