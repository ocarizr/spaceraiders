// SpaceRaiders.cpp : Defines the entry point for the console application.
#include <chrono>
#include <thread>

#include "PlayerShip.h"
#include "Alien.h"

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

    intRand xCoord(0, static_cast<int>(size.x-1));
    intRand yCoord(0, 10);

    // Populate aliens
    for (int k = 0; k < 20; k++)
    {
        Alien& a = *(new Alien);
        a.pos.x = static_cast<float>(xCoord(rGen));
        a.pos.y = static_cast<float>(yCoord(rGen));
        world.AddObject(&a);
    }
    // Add player
    PlayerShip& p = *(new PlayerShip);
    p.pos = Vector2D(40, 27);
    world.AddObject(&p);

//#define TEST_RUN
#ifdef TEST_RUN
    // Test run with random input
    world.controllerInput = new RndInput;
    while (world.HasPlayer())
#else
    // ToDo - implement keyboard (or other device input)
    world.controllerInput = new KeyboardInput;
    while (world.HasPlayer()) // break when game-over
#endif
    {
        world.Update();

        RenderItemList rl;
        for (auto it : world.GameObjects())
        {
            RenderItem a = RenderItem(Vector2D(it->pos), it->sprite);
            rl.push_back(a);
        }

        mainRenderer.Update(rl);
        // Sleep a bit so updates don't run too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.RemoveAllObjects();
    RenderItemList rl;
    mainRenderer.Update(rl);

    getchar();

    return 0;
}
