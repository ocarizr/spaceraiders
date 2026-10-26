#ifndef INPUT_H
#define INPUT_H
#include <random>
#include <windows.h>
#include <winuser.h>

extern std::default_random_engine rGen;
using intRand = std::uniform_int_distribution<int>;
using floatRand = std::uniform_real_distribution<float>;

// Legacy injection boundary. C04 will move RNG ownership into the native World.
class RandomSource
{
  public:
    virtual ~RandomSource() = default;

    // Returns a value in [0, 1). Implementations must outlive their consumers.
    virtual float Unit() = 0;
};

class EngineRandomSource final : public RandomSource
{
  public:
    float Unit() override
    {
        return floatRand(0.f, 1.f)(rGen);
    }
};

inline RandomSource& LegacyRandom()
{
    static EngineRandomSource source;
    return source;
}

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
    RandomSource& random;

  public:
    static constexpr float LeftChance = 0.3f;
    static constexpr float RightChance = 0.4f;
    static constexpr float FireChance = 0.5f;

    explicit RndInput(RandomSource& source = LegacyRandom()) : random(source)
    {
    }

    bool Left() override
    {
        return random.Unit() < LeftChance;
    }

    bool Right() override
    {
        return random.Unit() < RightChance;
    }

    bool Fire() override
    {
        return random.Unit() < FireChance;
    }
};

class KeyboardInput : public Input
{
  public:
    static bool IsPressed(short byte)
    {
        return static_cast<bool>(byte >> 15 & 0x1);
    }

    bool Left() override
    {
        return IsPressed(GetKeyState(VK_LEFT));
    }

    bool Right() override
    {
        return IsPressed(GetKeyState(VK_RIGHT));
    }

    bool Fire() override
    {
        return IsPressed(GetKeyState(VK_SPACE));
    }
};

class KeyboardWithRandomFire : public Input
{
    bool Left() override
    {
        return KeyboardInput::IsPressed(GetKeyState(VK_LEFT));
    }

    bool Right() override
    {
        return KeyboardInput::IsPressed(GetKeyState(VK_RIGHT));
    }

    bool Fire() override
    {
        return LegacyRandom().Unit() < RndInput::FireChance;
    }
};

#endif // INPUT_H
