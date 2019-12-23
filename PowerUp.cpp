#include "PlayerShip.h"

void TripleFire::ApplyPowerUp(GameObjPtr player)
{
    auto p = dynamic_cast<PlayerShip*>(player);
    p->StartTripleLaser();
}

void SpeedBoost::ApplyPowerUp(GameObjPtr player)
{
    auto p = dynamic_cast<PlayerShip*>(player);
    p->UpdateMovementSpeed(1.5f);
}

void FireBoost::ApplyPowerUp(GameObjPtr player)
{
    auto p = dynamic_cast<PlayerShip*>(player);
    p->UpdateFireDelay(.5f);
}
