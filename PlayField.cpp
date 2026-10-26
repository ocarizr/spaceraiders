#include "PlayField.h"

#include <algorithm>

#include "Alien.h"
#include "GameMode.h"
#include "Input.h"
#include "PlayerShip.h"
#include "BlockWall.h"

PlayField::PlayField(const Vector2D& iBounds)
    : bounds(iBounds), AlienLasers(InitialAlienLaserSlots), PlayerLasers(InitialPlayerLaserSlots)
{
}

PlayField::~PlayField()
{
    RemoveAllObjects();
}

bool PlayField::OwnsObject(GameObjPtr object) const
{
    const auto matches = [object](const auto& owned)
    {
        return owned.get() == object;
    };

    return std::ranges::any_of(gameObjects, matches) ||
           std::ranges::any_of(gameObjectsToAdd, matches);
}

bool PlayField::AddObject(GameObjPtr newObj)
{
    if (!newObj || OwnsObject(newObj))
    {
        return false;
    }

    // Own before allocating the queue node, so allocation failure also releases it.
    std::unique_ptr<GameObject> owned(newObj);
    gameObjectsToAdd.push_back(std::move(owned));
    return true;
}

bool PlayField::RemoveObject(GameObjPtr object)
{
    if (!OwnsObject(object))
    {
        return false;
    }

    return gameObjectsToRemove.insert(object).second;
}

void PlayField::RemoveAllObjects()
{
    gameObjectsToRemove.clear();
    gameObjectsToAdd.clear();
    gameObjects.clear();
}

void PlayField::ProcessQueues()
{
    const auto removed = [this](const auto& object)
    {
        return gameObjectsToRemove.contains(object.get());
    };

    std::erase_if(gameObjects, removed);
    std::erase_if(gameObjectsToAdd, removed);
    gameObjectsToRemove.clear();
    gameObjects.splice(gameObjects.end(), gameObjectsToAdd);
}

GameMode* PlayField::GetGameMode() const
{
    return gameMode.get();
}

void PlayField::SetGameMode(GameMode* value)
{
    if (gameMode.get() != value)
    {
        gameMode.reset(value);
    }
}

GameObjPtr PlayField::GetPlayerObject()
{
    for (auto object : GameObjects())
    {
        if (object->m_objType == "ot_PlayerShip")
        {
            return object;
        }
    }

    return nullptr;
}

bool PlayField::HasPlayer()
{
    return GetPlayerObject() != nullptr;
}

bool PlayField::HasAliens()
{
    return AlienCount() != 0;
}

int PlayField::AlienCount()
{
    return static_cast<int>(std::ranges::count_if(GameObjects(),
                                                  [](GameObjPtr object)
                                                  {
                                                      return object->m_objType == "ot_AlienShip";
                                                  }));
}

int PlayField::AlienLaserCount()
{
    return static_cast<int>(std::ranges::count_if(GameObjects(),
                                                  [](GameObjPtr object)
                                                  {
                                                      return object->m_objType == "ot_AlienLaser";
                                                  }));
}

void PlayField::SpawnLaser(GameObjPtr newObj)
{
    if (!AddObject(newObj))
    {
        return;
    }

    if (newObj->m_objType == "ot_AlienLaser")
    {
        AlienLasers--;
    }
    else if (newObj->m_objType == "ot_PlayerLaser")
    {
        PlayerLasers--;
    }
}

void PlayField::DespawnLaser(GameObjPtr object)
{
    if (!OwnsObject(object) || gameObjectsToRemove.contains(object))
    {
        return;
    }

    // Check before logical removal to preserve the existing alien budget rule.
    const bool returnAlienSlot = AlienLaserCount() < AlienCount() * AlienLaserSlotsPerAlien;
    if (!RemoveObject(object))
    {
        return;
    }

    if (object->m_objType == "ot_AlienLaser" && returnAlienSlot)
    {
        AlienLasers++;
    }
    else if (object->m_objType == "ot_PlayerLaser")
    {
        PlayerLasers++;
    }
}

void PlayField::Update()
{
    if (gameMode && gameMode->GameOver())
    {
        return;
    }

    // Commit the previous tick's wave/effects before mode queries. Otherwise a
    // pending wave can be mistaken for a cleared scene and appear after game over.
    ProcessQueues();

    // Resolve initial overlap and post-movement contact independently of entity
    // insertion order. Only the queue boundary physically destroys objects.
    const auto resolveContacts = [this]
    {
        for (auto object : GameObjects())
        {
            if (auto alien = dynamic_cast<Alien*>(object))
            {
                alien->ResolvePlayerContact(*this);
            }
        }
    };

    resolveContacts();
    if (gameMode && !HasPlayer())
    {
        gameMode->Update(*this);
        return;
    }

    for (auto item : GameObjects())
    {
        item->Update(*this);
    }

    resolveContacts();
    if (gameMode)
    {
        gameMode->Update(*this);
    }
}

void PlayField::AddAlienWave(Vector2D& canvasSize)
{
    if (gameMode && gameMode->GameOver())
    {
        return;
    }

    intRand xCoord(1, static_cast<int>(canvasSize.x - 2));
    intRand yCoord(0, 10);

    int amount;
    if (dynamic_cast<DestroyAllAliens*>(gameMode.get()))
    {
        amount = 50;
    }
    else
    {
        intRand amountOfAliens(15, 30);
        amount = amountOfAliens(rGen);
    }

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

    for (int i = 0; i < amount; ++i)
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
