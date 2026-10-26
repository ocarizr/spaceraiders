// SpaceRaiders.cpp : Defines the entry point for the console application.
#include <chrono>
#include <memory>
#include <thread>
#include <iostream>

#include "PlayerShip.h"
#include "Alien.h"
#include "BlockWall.h"

#include "GameMenus.h"

std::default_random_engine rGen;

const int canvasWidth = 80;
const int canvasHeight = 28;

int main()
{
    rGen.seed(1);
    Vector2D size(canvasWidth, canvasHeight);
    Renderer mainRenderer(size);
    PlayField world(size);

    {
        RenderItemList rl;
        GameMenus::GenerateStartMenu(rl, size);
        mainRenderer.Update(rl, world);
        int modeSelected = 0;

        do
        {
            modeSelected = GameMenus::SelectOptionFromStartMenu();
        } while (modeSelected == 0);

        switch (modeSelected)
        {
        case 1:
            world.SetGameMode(new UntilPlayerDeath);
            break;
        case 2:
            world.SetGameMode(new TimeAttack);
            break;
        case 3:
            world.SetGameMode(new DestroyAllAliens);
            break;
        }
    }

    // Populate aliens
    world.AddAlienWave(size);
    // Add player
    world.AddPlayer(size);
    world.ProcessQueues();

// #define TEST_RUN
#ifdef TEST_RUN
    world.controllerInput = std::make_unique<KeyboardInput>();
#else
    world.controllerInput = std::make_unique<KeyboardWithRandomFire>();
#endif
    // Menu/setup time is not simulated. Each subsequent sample includes update,
    // rendering and sleeping time since the preceding sample.
    world.vsync.Reset();
    while (!world.GetGameMode()->GameOver()) // break when game-over
    {
        const int ticks = world.vsync.Advance(VSync::Clock::now());
        for (int tick = 0; tick < ticks && !world.GetGameMode()->GameOver(); ++tick)
        {
            world.Update();
            world.ProcessQueues();
            if (!world.HasAliens())
            {
                world.AddAlienWave(size);
            }
        }

        if (ticks > 0)
        {
            RenderItemList rl;
            for (auto it : world.GameObjects())
            {
                RenderItem a = RenderItem(Vector2D(it->pos), it->sprite);
                rl.push_back(a);
            }

            mainRenderer.Update(rl, world);
        }

        if (!world.GetGameMode()->GameOver())
        {
            std::this_thread::sleep_for(world.vsync.TimeUntilNextTick(VSync::Clock::now()));
        }
    }

    {
        world.RemoveAllObjects();
        RenderItemList rl;
        GameMenus::GenerateGameOverMenu(rl, size, world.GetGameMode()->GetScore(),
                                        world.HasPlayer());
        mainRenderer.Update(rl, world);
    }
    getchar(); // just to wait the player to confirm the close game

    return 0;
}
