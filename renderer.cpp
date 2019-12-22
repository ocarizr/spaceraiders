#include <vector>
#include <iomanip>
#include <iostream>
#include "renderer.h"

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


Renderer::~Renderer() noexcept
{
    delete[] disp[0].canvas;
    delete[] disp[1].canvas;
}

void Renderer::Update(const RenderItemList& RenderList, const PlayField& world)
{
    FillCanvas(RS_BackgroundTile);

    for (const auto& ri : RenderList)
    {
        // std::cout << "object drawn" << std::endl;
        int x = int(ri.pos.x);
        int y = int(ri.pos.y);

        if (x >= 0 && x < static_cast<int>(renderBounds.x) && y >= 0 && y < static_cast<int>(renderBounds.y))
        {
            *CurCanvas(x, y) = ri.sprite;
        }
    }

    DrawCanvas(world);
}

void Renderer::FillCanvas(unsigned char sprite) noexcept
{
    for (int i = 0; i < canvasSize; i++)
    {
        *CurCanvas(i,0) = sprite;
    }
}

void Renderer::DrawCanvas(const PlayField& world)
{
    std::stringstream canvas;
    auto gameMode = world.GetGameMode();

    {
        auto ta = dynamic_cast<TimeAttack*>(gameMode);
        if (ta)
        {
            canvas << "Time: " << std::setw(2) << std::setfill('0') << ta->GetTimer() << '\t';
        }
    }

    if (gameMode)
        canvas << "Score: " << gameMode->GetScore() << '\n';

    for (int y = 0; y < static_cast<int>(renderBounds.y); ++y)
    {
        for (int x = 0; x < static_cast<int>(renderBounds.x); ++x)
        {
            canvas << *CurCanvas(x,y);
        }
        canvas << '\n';
    }
    setCursorPosition(0, 0);
    std::cout << canvas.str();
    curIdx++;
}
