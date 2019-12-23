#ifndef PLAYFIELD_H
#define PLAYFIELD_H
#include <list>
#include <deque>
#include <algorithm>

#include "GameObject.h"
#include "Input.h"
#include "VSync.h"

class GameMode;

class PlayField
{
private:
    std::list<GameObjPtr> gameObjects;

    std::deque<GameObjPtr> gameObjectsToAdd;
    std::deque<GameObjPtr> gameObjectsToRemove;

    GameMode* gameMode = nullptr;

public:
    Input* controllerInput = nullptr;
    Vector2D bounds;
    VSync vsync;
    // Number of available active laser slots for aliens and player
    int AlienLasers = 10;
    int PlayerLasers = 4;

    explicit PlayField(const Vector2D& iBounds) : bounds(iBounds) {}

    ~PlayField();

    const std::list<GameObjPtr>& GameObjects() { return gameObjects; }

    void Update();

    GameObjPtr GetPlayerObject()
    {
        auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [](GameObjPtr in) { return in->m_objType == "ot_PlayerShip"; });
        if (it != gameObjects.end())
            return (*it);
        return nullptr;
    }

    bool HasPlayer()
    {
        return GetPlayerObject() != nullptr;
    }

    bool HasAliens()
    {
        auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [](GameObjPtr in){ return in->m_objType == "ot_AlienShip"; });
        return it != GameObjects().end();
    }

    void SpawnLaser(GameObjPtr newObj)
    {
        if (newObj->m_objType == "ot_AlienLaser")
            AlienLasers--;
        else if (newObj->m_objType == "ot_PlayerLaser")
            PlayerLasers--;
        AddObject(newObj);
    }

    void DespawnLaser(GameObjPtr newObj)
    {
        if (newObj->m_objType == "ot_AlienLaser")
            AlienLasers++;
        else if (newObj->m_objType == "ot_PlayerLaser")
            PlayerLasers++;
        RemoveObject(newObj);
    }

    void AddObject(GameObjPtr newObj)
    {
        gameObjectsToAdd.push_back(newObj);
    }

    void RemoveObject(GameObjPtr newObj)
    {
        gameObjectsToRemove.push_back(newObj);
    }

    void RemoveAllObjects()
    {
        for(auto& item : gameObjects)
        {
            delete item;
        }
        gameObjects.clear();
    }

    void ProcessQueues()
    {
        for (auto& item : gameObjectsToAdd)
        {
            gameObjects.push_back(item);
        }
        gameObjectsToAdd.clear();

        for(auto& item : gameObjectsToRemove)
        {
            auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](auto& go){return go == item;});
            if(it != gameObjects.end())
            {
                delete *it;
                gameObjects.erase(it);
            }
        }
        gameObjectsToRemove.clear();
    }

    [[nodiscard]] GameMode* GetGameMode() const{ return gameMode; }

    void SetGameMode(GameMode* value){ gameMode = value; }
};

#endif // PLAYFIELD_H
