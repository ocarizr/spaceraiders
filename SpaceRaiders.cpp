// SpaceRaiders.cpp : Defines the entry point for the console application.
#include <thread>
#include <iostream>

#include "PlayerShip.h"
#include "Alien.h"

std::default_random_engine rGen;

/*
static char ObjectType[][64] =
{
        "ot_AlienShip",
        "ot_PlayerShip",
        "ot_AlienLaser",
        "ot_PlayerLaser",
        "ot_Explosion"
};
*/

int main()
{
    rGen.seed(1);
    Vector2D size(80, 28);
    Renderer mainRenderer(size);
    PlayField world(size);

    world.SetGameMode(new TimeAttack);

    intRand xCoord(0, static_cast<int>(size.x-1));
    intRand yCoord(0, 10);

    // Populate aliens
    for (int k = 0; k < 20; k++)
    {
        auto a = new Alien;
        a->pos.x = static_cast<float>(xCoord(rGen));
        a->pos.y = static_cast<float>(yCoord(rGen));
        world.AddObject(a);
    }
    // Add player
    auto p = new PlayerShip;
    p->pos = Vector2D(40, 27);
    world.AddObject(p);

    world.ProcessQueues();

//#define TEST_RUN
#ifdef TEST_RUN
    world.controllerInput = new RndInput;
#else
    world.controllerInput = new KeyboardInput;
#endif
    while (!world.GetGameMode()->GameOver()) // break when game-over
    {
        auto start = std::chrono::high_resolution_clock::now();
        world.Update();
        world.ProcessQueues();

        RenderItemList rl;
        for (auto it : world.GameObjects())
        {
            RenderItem a = RenderItem(Vector2D(it->pos), it->sprite);
            rl.push_back(a);
        }

        mainRenderer.Update(rl, world);
        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::nanoseconds timestamp = finish - start;
        int convertToMilli = static_cast<int>(timestamp.count() / 1000000);
        // Sleep a bit so updates don't run too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(world.vsync.GetSyncPeriod(convertToMilli)));
    }
    world.RemoveAllObjects();
    RenderItemList rl;
    mainRenderer.Update(rl, world);

    getchar();

    return 0;
}
