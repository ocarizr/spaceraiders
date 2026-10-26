#ifndef GAMEMODE_H
#define GAMEMODE_H
#include <algorithm>
#include <chrono>

#include "PlayField.h"

enum class GameOutcome
{
    Running,
    Won,
    Lost
};

class GameMode
{
  protected:
    int score;
    GameOutcome outcome = GameOutcome::Running;

  public:
    GameMode() : score(0)
    {
    }

    virtual ~GameMode() = default;

    int GetScore() const noexcept
    {
        return score;
    }

    void UpdateScore(int value) noexcept
    {
        score += value;
    }

    bool GameOver() const noexcept
    {
        return outcome != GameOutcome::Running;
    }

    GameOutcome GetOutcome() const noexcept
    {
        return outcome;
    }

    virtual void Update(PlayField& world) = 0;
};

class TimeAttack : public GameMode
{
    static constexpr std::chrono::milliseconds MatchDuration{std::chrono::seconds{59}};
    std::chrono::milliseconds remaining = MatchDuration;

    void Update(PlayField& world) override
    {
        if (GameOver())
        {
            return;
        }

        remaining =
            (std::max)(remaining - VSync::SimulationStep, std::chrono::milliseconds::zero());

        // Losing the player takes priority over reaching the timer boundary.
        if (!world.HasPlayer())
        {
            outcome = GameOutcome::Lost;
        }
        else if (remaining == std::chrono::milliseconds::zero())
        {
            outcome = GameOutcome::Won;
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
        if (!GameOver() && !world.HasPlayer())
        {
            outcome = GameOutcome::Lost;
        }
    }
};

class DestroyAllAliens : public GameMode
{
    void Update(PlayField& world) override
    {
        if (GameOver())
        {
            return;
        }

        if (!world.HasPlayer())
        {
            outcome = GameOutcome::Lost;
        }
        else if (!world.HasAliens())
        {
            outcome = GameOutcome::Won;
        }
    }
};

#endif // GAMEMODE_H
