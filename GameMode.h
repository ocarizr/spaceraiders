#ifndef GAMEMODE_H
#define GAMEMODE_H
#include <algorithm>
#include <chrono>

#include "PlayField.h"

class GameMode
{
  protected:
    int score;
    bool gameOver;

  public:
    GameMode() : score(0), gameOver(false)
    {
    }

    virtual ~GameMode() = default;

    int GetScore() noexcept
    {
        return score;
    }

    void UpdateScore(int value) noexcept
    {
        score += value;
    }

    bool GameOver() noexcept
    {
        return gameOver;
    }

    virtual void Update(PlayField& world) = 0;
};

class TimeAttack : public GameMode
{
    static constexpr std::chrono::milliseconds MatchDuration{std::chrono::seconds{59}};
    std::chrono::milliseconds remaining = MatchDuration;

    void Update(PlayField& world) override
    {
        if (gameOver)
        {
            return;
        }

        remaining =
            (std::max)(remaining - VSync::SimulationStep, std::chrono::milliseconds::zero());

        if (remaining == std::chrono::milliseconds::zero() || !world.HasPlayer())
        {
            gameOver = true;
        }
    }

  public:
    [[nodiscard]] int GetTimer() const noexcept
    {
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(remaining).count());
    }
};

class UntilPlayerDeath : public GameMode
{
    void Update(PlayField& world) override
    {
        gameOver = !world.HasPlayer();
    }
};

class DestroyAllAliens : public GameMode
{
    void Update(PlayField& world) override
    {
        gameOver = !world.HasAliens() || !world.HasPlayer();
    }
};

#endif // GAMEMODE_H
