#ifndef GAMEMODE_H
#define GAMEMODE_H
#include "PlayField.h"

class GameMode
{
protected:
    bool gameOver;
    int score;
public:
    GameMode() : gameOver(false) {}
    virtual ~GameMode() = default;

    int GetScore() noexcept { return score; }
    void UpdateScore(int value) noexcept { score += value; }
    bool GameOver() noexcept { return gameOver; }
    virtual void Update(PlayField& world) = 0;
};

class TimeAttack : public GameMode
{
    int timer = 30000;

    void Update(PlayField& world) override
    {
        timer -= world.vsync.GetFramePeriod();

        if(timer <= 0)
        {
            gameOver = true;
        }
    }

public:
    int GetTimer() const noexcept { return timer / 1000; }
};

class SingleLife : public GameMode
{
    void Update(PlayField& world) override
    {
        gameOver = !world.HasPlayer();
    }
};

class Normal : public GameMode
{
    void Update(PlayField& world) override
    {
        // implement ASAP
        gameOver = true;
    }
};

#endif // GAMEMODE_H
