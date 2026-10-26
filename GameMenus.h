#ifndef GAMEMENUS_H
#define GAMEMENUS_H
#include <windows.h>
#include <string>
#include <list>
#include <utility>

#include "Vector2D.h"
#include "renderer.h"
#include "GameMode.h"
#include "PlayField.h"

class GameMenus
{
    using ScreenMsg = std::list<std::pair<unsigned char, Vector2D>>;

    static const ScreenMsg LayoutToPrint(int widht, int msgHeight, std::string msg)
    {
        ScreenMsg ret;

        int slotToStart = (widht / 2) - msg.size();
        Vector2D pos(0, msgHeight);

        for (int i = 0; i < slotToStart; ++i)
        {
            pos.x = i;
            ret.emplace_back(RS_BackgroundTile, pos);
        }

        for (const auto& letter : msg)
        {
            pos.x = slotToStart;
            ++slotToStart;
            ret.emplace_back(letter == ' ' ? RS_BackgroundTile : letter, pos);
            pos.x = slotToStart;
            ++slotToStart;
            ret.emplace_back(RS_BackgroundTile, pos);
        }

        for (int i = slotToStart; i < widht; ++i)
        {
            pos.x = i;
            ret.emplace_back(RS_BackgroundTile, pos);
        }

        return ret;
    }

  public:
    static void GenerateGameOverFrame(RenderItemList& rl, const PlayField& world)
    {
        for (auto object : world.GameObjects())
        {
            rl.emplace_back(object->pos, object->sprite);
        }

        auto mode = world.GetGameMode();
        if (mode && mode->GameOver())
        {
            GenerateGameOverMenu(rl, world.bounds, mode->GetScore(),
                                 mode->GetOutcome() == GameOutcome::Won);
        }
    }

    static void GenerateStartMenu(RenderItemList& rl, const Vector2D canvasSize)
    {
        auto Title = GameMenus::LayoutToPrint(
            static_cast<int>(canvasSize.x), static_cast<int>(canvasSize.y) / 2 - 2, "SpaceRaiders");
        auto msg =
            GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                     static_cast<int>(canvasSize.y) / 2 - 1, "Select a Game Mode:");
        auto firstOption =
            GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                     static_cast<int>(canvasSize.y) / 2, "1 - Until Player Death.");
        auto secondOption = GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                                     static_cast<int>(canvasSize.y) / 2 + 1,
                                                     "2 - Time Attack (59 seconds).");
        auto thirdOption = GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                                    static_cast<int>(canvasSize.y) / 2 + 2,
                                                    "3 - Destroy All Aliens.");

        for (const auto& pair : Title)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : msg)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : firstOption)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : secondOption)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : thirdOption)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }
    }

    static void GenerateGameOverMenu(RenderItemList& rl, const Vector2D canvasSize, const int score,
                                     bool won)
    {
        auto gameOverMsg = GameMenus::LayoutToPrint(
            static_cast<int>(canvasSize.x), static_cast<int>(canvasSize.y) / 2 - 1, "Game Over");
        auto GameResult = GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                                   static_cast<int>(canvasSize.y) / 2,
                                                   won ? "You won the game!" : "You Died!");
        auto ScoreMsg = GameMenus::LayoutToPrint(static_cast<int>(canvasSize.x),
                                                 static_cast<int>(canvasSize.y) / 2 + 1,
                                                 "Score: " + std::to_string(score));

        for (const auto& pair : gameOverMsg)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : GameResult)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }

        for (const auto& pair : ScoreMsg)
        {
            RenderItem a = RenderItem(pair.second, pair.first);
            rl.push_back(a);
        }
    }

    [[nodiscard]] static int SelectOptionFromStartMenu()
    {
        int ret = 0;

        if (GetKeyState(0x31) >> 15 & 0x1)
        {
            ret = 1;
        }
        else if (GetKeyState(0x32) >> 15 & 0x1)
        {
            ret = 2;
        }
        else if (GetKeyState(0x33) >> 15 & 0x1)
        {
            ret = 3;
        }

        return ret;
    }
};

#endif // GAMEMENUS_H
