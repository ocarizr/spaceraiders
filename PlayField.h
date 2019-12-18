#ifndef PLAYFIELD_H
#define PLAYFIELD_H
#include <cstring>
#include <vector>
#include <algorithm>

#include "GameObject.h"
#include "Input.h"

class PlayField
{
private:
    typedef GameObject* GameObjPtr;
    std::vector<GameObjPtr> gameObjects;

public:
    Input* controllerInput = nullptr;
    Vector2D bounds;
    // Number of available active laser slots for aliens and player
    int AlienLasers = 10;
    int PlayerLasers = 4;

    PlayField(Vector2D iBounds) : bounds(iBounds) {}
    ~PlayField();
    const std::vector<GameObjPtr>& GameObjects() { return gameObjects; }

    void Update()
    {
        int amount = static_cast<int>(gameObjects.size());
        // Update list of active objects in the world
        // for (auto it : gameObjects)
        for (int index = 0; index < amount; ++index)
        {
            gameObjects[index]->Update(*this);
            amount = std::min(amount, static_cast<int>(gameObjects.size()));
        }
    }

    GameObject* GetPlayerObject()
    {
        auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [](GameObjPtr in) { return (strcmp(in->m_objType, "ot_PlayerShip") == 0); });
        if (it != gameObjects.end())
            return (*it);
        return nullptr;
    }

    bool HasPlayer()
    {
        return GetPlayerObject() != nullptr;
    }

    //
    void SpawnLaser(GameObject* newObj)
    {
        if (strcmp(newObj->m_objType, "ot_AlienLaser") == 0)
            AlienLasers--;
        else if (strcmp(newObj->m_objType, "ot_PlayerLaser") == 0)
            PlayerLasers--;
        AddObject(newObj);
    }
    void DespawnLaser(GameObject* newObj)
    {
        if (strcmp(newObj->m_objType, "ot_AlienLaser") == 0)
            AlienLasers++;
        else if (strcmp(newObj->m_objType, "ot_PlayerLaser") == 0)
            PlayerLasers++;
        RemoveObject(newObj);
    }

    void AddObject(GameObject* newObj)
    {
        //gameObjectsToAdd.push_back(GameObjPtr(newObj));
        gameObjects.push_back(newObj);
    }
    void RemoveObject(GameObject* newObj)
    {
        //gameObjectsToRemove.push_back(newObj);
        auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](GameObjPtr in) { return (in == newObj); });
        gameObjects.erase(it);
    }

    void RemoveAllObjects()
    {
        while(!gameObjects.empty())
        {
            gameObjects.erase(gameObjects.begin());
        }
    }
};

PlayField::~PlayField()
{
    if(controllerInput)
        delete controllerInput;
}

#endif // PLAYFIELD_H
