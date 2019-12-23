#include "Alien.h"
#include "PlayerShip.h"
#include "BlockWall.h"

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

void PlayField::AddAlienWave(Vector2D& canvasSize)
{
    intRand xCoord(1, static_cast<int>(canvasSize.x - 2));
    intRand yCoord(0, 10);

    intRand amountOfAliens(15, 30);
    int amount = amountOfAliens(rGen);

    for (int k = 0; k < amount; ++k)
    {
        auto a = new Alien;
        a->pos.x = static_cast<float>(xCoord(rGen));
        a->pos.y = static_cast<float>(yCoord(rGen));
        AddObject(a);
    }
    AlienLasers = amount;

    AddBlockWalls(canvasSize);
}

void PlayField::AddBlockWalls(Vector2D& canvasSize)
{
    intRand xCoord(5, static_cast<int>(canvasSize.x - 5));
    intRand yCoord(5, 15);

    intRand amountOfWalls(5, 15);
    auto amount = amountOfWalls(rGen);

    for(int i = 0; i < amount; ++i)
    {
        auto b = new BlockWall;
        b->pos.x = static_cast<float>(xCoord(rGen));
        b->pos.y = static_cast<float>(yCoord(rGen));
        AddObject(b);
    }
}

void PlayField::AddPlayer(Vector2D& canvasSize)
{
    auto p = new PlayerShip;
    p->pos = Vector2D(canvasSize.x / 2, 27);
    AddObject(p);
}
