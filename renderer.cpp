#include <vector>
#include <iostream>
#include <windows.h>
#include "Renderer.h"

void setCursorPosition(int x, int y)
{
    static const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    std::cout.flush();
    COORD coord = { static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(hOut, coord);
}

Renderer::Renderer(const Vector2D& bounds)
    : renderBounds(bounds)
{
    canvasSize = static_cast<int>(bounds.x * bounds.y);
    disp[0].canvas = new unsigned char[canvasSize];
    disp[1].canvas = new unsigned char[canvasSize];
}


Renderer::~Renderer()
{
    delete[] disp[0].canvas;
    delete[] disp[1].canvas;
}

void Renderer::Update(const RenderItemList& RenderList, PlayField& world)
{
    FillCanvas(RS_BackgroundTile);

    for (auto ri : RenderList)
    {
        // std::cout << "object drawn" << std::endl;
        int x = int(ri.pos.x);
        int y = int(ri.pos.y);

        if (x >= 0 && x < renderBounds.x && y >= 0 && y < renderBounds.y)
        {
            *CurCanvas(static_cast<int>(ri.pos.x), static_cast<int>(ri.pos.y)) = static_cast<unsigned char>(ri.sprite);
        }
    }

    DrawCanvas(world);
}

void Renderer::FillCanvas(unsigned char sprite)
{
    for (int i = 0; i < canvasSize; i++)
    {
        *CurCanvas(i,0) = sprite;
    }
}

void Renderer::DrawCanvas(PlayField& world)
{
    auto gameMode = world.GetGameMode();
    {
        auto ta = dynamic_cast<TimeAttack*>(gameMode);
        if(ta)
        {
            std::cout << "Time: " << ta->GetTimer() << '\t';
        }
    }
    if (gameMode)
        std::cout << "Score: " << gameMode->GetScore() << std::endl;

    for (int y = 0; y < renderBounds.y; y++)
    {
        for (int x = 0; x < renderBounds.x; x++)
        {
            setCursorPosition(x, y);
            std::cout << *CurCanvas(x,y);
        }
        std::cout << std::endl;
    }

    curIdx++;
}
