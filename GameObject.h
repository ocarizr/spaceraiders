#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H
#include <string>
#include "Vector2D.h"

class PlayField;

class GameObject
{
public:
    GameObject() = default;
    GameObject(std::string objType, unsigned char s) : m_objType(std::move(objType)), pos(), sprite(s){}
    virtual ~GameObject() = default;

    std::string m_objType;
	Vector2D pos;
	unsigned char sprite{};

    virtual void Update(PlayField& world) = 0;
    virtual bool DecreaseHealth(PlayField& world) { return true; }
};

typedef GameObject* GameObjPtr;

#endif // GAMEOBJECT_H
