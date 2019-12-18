#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H
#include "Vector2D.h"

class PlayField;

class GameObject
{
public:
	virtual ~GameObject() = default;

	char* m_objType = nullptr;
	Vector2D pos;
	unsigned char sprite;

    virtual void Update(PlayField& world) = 0;
    virtual bool DecreaseHealth() { return true; }
};

#endif // GAMEOBJECT_H
