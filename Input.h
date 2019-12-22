#ifndef INPUT_H
#define INPUT_H
#include <random>
#include <windows.h>
#include <winuser.h>

extern std::default_random_engine rGen;
using intRand = std::uniform_int_distribution<int>;
using floatRand = std::uniform_real_distribution<float>;

class Input
{
public:
    virtual ~Input() = default;

    virtual bool Left() = 0;
    virtual bool Right() = 0;
    virtual bool Fire() = 0;
};

class RndInput : public Input
{
public:
    bool Left() override { floatRand keyRate(0, 1); return (keyRate(rGen) < 0.3f); }
    bool Right() override { floatRand keyRate(0, 1); return (keyRate(rGen) < 0.4f); }
    bool Fire() override { floatRand keyRate(0, 1); return (keyRate(rGen) < 0.5f); }
};

class KeyboardInput : public Input
{
    static bool IsPressed(short byte)
    {
        return static_cast<bool>(byte >> 15 & 0x1);
    }

public:
    bool Left() override { return IsPressed(GetKeyState(0x25)); }
    bool Right() override { return IsPressed(GetKeyState(0x27)); }
    bool Fire() override { return IsPressed(GetKeyState(0x20)); }
};

#endif // INPUT_H
