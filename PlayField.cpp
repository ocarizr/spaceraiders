#include "GameMode.h"

PlayField::~PlayField()
{
    delete controllerInput;
    delete gameMode;
}

void PlayField::Update()
{
    // Update list of active objects in the world
    for (auto item : gameObjects)
    {
        item->Update(*this);
    }
    gameMode->Update(*this);
}
