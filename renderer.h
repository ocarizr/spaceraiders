#ifndef RENDERER_H
#define RENDERER_H
#include <vector>
#include "Vector2D.h"
#include "GameMode.h"

struct RenderItem
{
    RenderItem(const Vector2D& iPos, unsigned char iSprite) : pos(iPos), sprite(iSprite) {}
    Vector2D pos;
    unsigned char sprite;
};
typedef std::vector<RenderItem> RenderItemList;

enum RaiderSprites
{
    RS_BackgroundTile = 0xB0,
    RS_Player = 'P',
    RS_Alien = 'A',
    RS_BetterAlien = 'B',
    RS_PlayerLaser = 0xBA,
    RS_AlienLaser = '|',
    RS_Explosion = '*'
};

class Renderer
{
public:
    Renderer(const Vector2D& bounds);
    ~Renderer();

    // Draws all game objects after clearing filling the Canvas with _ symbol
    void Update(const RenderItemList& renderList, PlayField& world);

private:
    Vector2D renderBounds;
    int curIdx = 0;
    struct
    {
        unsigned char* canvas = nullptr;
    } disp[2]; // double buffer our canvas for no flicker display
    int canvasSize = 0;
    unsigned char* CurCanvas(int x, int y) { return &disp[curIdx % 2].canvas[x + static_cast<int>(renderBounds.x) * y];  }

    // Fills whole canvas array with sprite
    void FillCanvas(unsigned char sprite);

    // Prints canvas char array on console
    void DrawCanvas(PlayField& world);
};

#endif // RENDERER_H
