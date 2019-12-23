// SpaceRaiders.cpp : Defines the entry point for the console application.
#include <thread>
#include <iostream>

#include "PlayerShip.h"
#include "Alien.h"
#include "BlockWall.h"

std::default_random_engine rGen;

const int canvasWidth = 80;
const int canvasHeight = 28;

int main()
{
    rGen.seed(1);
    Vector2D size(canvasWidth, canvasHeight);
    Renderer mainRenderer(size);
    PlayField world(size);

    world.SetGameMode(new UntilPlayerDeath);

    // Populate aliens
    world.AddAlienWave(size);
    // Add player
    world.AddPlayer(size);
    world.ProcessQueues();

//#define TEST_RUN
#ifdef TEST_RUN
    world.controllerInput = new KeyboardInput;
#else
    world.controllerInput = new KeyboardWithRandomFire;
#endif
    while (!world.GetGameMode()->GameOver()) // break when game-over
    {
        auto start = std::chrono::high_resolution_clock::now();
        world.Update();
        world.ProcessQueues();
        if(!world.HasAliens()) world.AddAlienWave(size);

        RenderItemList rl;
        for (auto it : world.GameObjects())
        {
            RenderItem a = RenderItem(Vector2D(it->pos), it->sprite);
            rl.push_back(a);
        }

        mainRenderer.Update(rl, world);
        auto finish = std::chrono::high_resolution_clock::now();

        std::chrono::nanoseconds timestamp = finish - start;
        const int convertToMilli = static_cast<int>(timestamp.count() / 1000000);
        // Sleep a bit so updates don't run too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(world.vsync.GetSyncPeriod(convertToMilli)));
    }
    world.RemoveAllObjects();
    RenderItemList rl;
    mainRenderer.Update(rl, world);

    getchar();

    return 0;
}
