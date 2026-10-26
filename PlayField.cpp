#include "PlayField.h"

#include <algorithm>
#include <stdexcept>

#include "Alien.h"
#include "GameMode.h"
#include "Input.h"
#include "PlayerShip.h"
#include "BlockWall.h"
#include "PlayerLaser.h"
#include "AlienLaser.h"
#include "Explosion.h"

PlayField::PlayField(const Vector2D& iBounds) : bounds(iBounds)
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
    const auto owner = GetLaserOwner(newObj);
    if (owner != LaserOwner::None &&
        (AvailableLaserSlots(owner) == 0 || (gameMode && gameMode->GameOver())))
    {
        return false;
    }

    gameObjectsToAdd.push_back(std::move(owned));
    ReconcileLaserSlots(owner);
    return true;
}

bool PlayField::RemoveObject(GameObjPtr object)
{
    if (!OwnsObject(object))
    {
        return false;
    }

    if (!gameObjectsToRemove.insert(object).second)
    {
        return false;
    }

    ReconcileLaserSlots(GetLaserOwner(object));
    return true;
}

void PlayField::RemoveAllObjects()
{
    gameObjectsToRemove.clear();
    gameObjectsToAdd.clear();
    gameObjects.clear();
    playerLaserCapacity = InitialPlayerLaserSlots;
    alienLaserCapacity = InitialAlienLaserSlots;
    ReconcileLaserSlots(LaserOwner::Player);
    ReconcileLaserSlots(LaserOwner::Alien);
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
    ReconcileLaserSlots(LaserOwner::Player);
    ReconcileLaserSlots(LaserOwner::Alien);
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

PlayField::LaserOwner PlayField::GetLaserOwner(GameObjPtr object)
{
    if (dynamic_cast<PlayerLaser*>(object))
    {
        return LaserOwner::Player;
    }

    return dynamic_cast<AlienLaser*>(object) ? LaserOwner::Alien : LaserOwner::None;
}

int PlayField::AvailableLaserSlots(LaserOwner owner) const
{
    const auto occupied = [this, owner](const auto& object)
    {
        return !gameObjectsToRemove.contains(object.get()) && GetLaserOwner(object.get()) == owner;
    };

    const auto count = std::ranges::count_if(gameObjects, occupied) +
                       std::ranges::count_if(gameObjectsToAdd, occupied);
    const int capacity = owner == LaserOwner::Player ? playerLaserCapacity : alienLaserCapacity;
    return count >= capacity ? 0 : capacity - static_cast<int>(count);
}

void PlayField::ReconcileLaserSlots(LaserOwner owner)
{
    if (owner == LaserOwner::Player)
    {
        availablePlayerLaserSlots = AvailableLaserSlots(owner);
    }
    else if (owner == LaserOwner::Alien)
    {
        availableAlienLaserSlots = AvailableLaserSlots(owner);
    }
}

void PlayField::SetPlayerLaserCapacity(int capacity)
{
    if (capacity < 0)
    {
        throw std::invalid_argument("Player laser capacity must be nonnegative.");
    }

    playerLaserCapacity = capacity;
    ReconcileLaserSlots(LaserOwner::Player);
}

void PlayField::SetAlienLaserCapacity(int capacity)
{
    if (capacity < 0)
    {
        throw std::invalid_argument("Alien laser capacity must be nonnegative.");
    }

    alienLaserCapacity = capacity;
    ReconcileLaserSlots(LaserOwner::Alien);
}

bool PlayField::SpawnLaser(GameObjPtr newObj)
{
    if (OwnsObject(newObj))
    {
        return false;
    }

    if (GetLaserOwner(newObj) == LaserOwner::None)
    {
        // The single-shot API consumes new invalid objects just like a rejected batch.
        std::unique_ptr<GameObject> rejected(newObj);
        return false;
    }

    return AddObject(newObj);
}

bool PlayField::SpawnPlayerLasers(std::vector<std::unique_ptr<GameObject>> lasers)
{
    if (lasers.empty() || (gameMode && gameMode->GameOver()) ||
        lasers.size() > static_cast<std::size_t>(AvailableLaserSlots(LaserOwner::Player)))
    {
        return false;
    }

    std::list<std::unique_ptr<GameObject>> staged;
    for (auto& laser : lasers)
    {
        if (GetLaserOwner(laser.get()) != LaserOwner::Player)
        {
            return false;
        }

        staged.push_back(std::move(laser));
    }

    // Allocate/validate the complete volley before the no-throw ownership transfer.
    gameObjectsToAdd.splice(gameObjectsToAdd.end(), staged);
    ReconcileLaserSlots(LaserOwner::Player);
    return true;
}

bool PlayField::IsLiveObject(GameObjPtr object) const
{
    return OwnsObject(object) && !gameObjectsToRemove.contains(object);
}

void PlayField::DespawnLaser(GameObjPtr object)
{
    if (IsLiveObject(object) && GetLaserOwner(object) != LaserOwner::None)
    {
        RemoveObject(object);
    }
}

bool PlayField::ResolveLaserCollision(GameObjPtr laser)
{
    if (!IsLiveObject(laser) || (gameMode && gameMode->GameOver()))
    {
        return true;
    }

    const auto owner = GetLaserOwner(laser);
    if (owner == LaserOwner::None)
    {
        return true;
    }

    if (laser->pos.x < 0 || laser->pos.x >= bounds.x || laser->pos.y < 0 ||
        laser->pos.y >= bounds.y)
    {
        DespawnLaser(laser);
        return true;
    }

    GameObjPtr target = nullptr;
    // Opposing projectiles intercept before ship/wall contacts. Friendly shots pass.
    for (auto object : GameObjects())
    {
        const auto otherOwner = GetLaserOwner(object);
        if (otherOwner != LaserOwner::None && otherOwner != owner && object->pos.IntCmp(laser->pos))
        {
            target = object;
            break;
        }
    }

    if (!target)
    {
        for (auto object : GameObjects())
        {
            const bool eligible =
                dynamic_cast<BlockWall*>(object) ||
                (owner == LaserOwner::Player ? dynamic_cast<Alien*>(object) != nullptr
                                             : dynamic_cast<PlayerShip*>(object) != nullptr);
            if (eligible && object->pos.IntCmp(laser->pos))
            {
                target = object;
                break;
            }
        }
    }

    if (!target)
    {
        return false;
    }

    DespawnLaser(laser);
    if (GetLaserOwner(target) != LaserOwner::None)
    {
        DespawnLaser(target);
    }
    else if ((owner == LaserOwner::Player && target->DecreaseHealth(*this)) ||
             (owner == LaserOwner::Alien && dynamic_cast<PlayerShip*>(target)))
    {
        RemoveObject(target);
    }

    auto explosion = std::make_unique<Explosion>();
    explosion->pos = laser->pos;
    AddObject(explosion.release());
    return true;
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

    SetAlienLaserCapacity(amount);

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
