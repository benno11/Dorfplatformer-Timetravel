#pragma once
#include <SDL2/SDL.h>
#include <vector>

struct TileMap {
    int tileSize = 32;
    int w = 16, h = 9;
    std::vector<unsigned char> solid;
    std::vector<unsigned char> semisolid;
    std::vector<unsigned char> water;
    std::vector<unsigned short> tileIds;
    std::vector<unsigned short> bg;

    TileMap();

    void resize(int W, int H);
    unsigned char getSolid(int x, int y) const;
    unsigned char getSemiSolid(int x, int y) const;
    unsigned char getWater(int x, int y) const;
    void setSolid(int x, int y, unsigned char v);
    void setSemiSolid(int x, int y, unsigned char v);
    void setWater(int x, int y, unsigned char v);

    void renderBgDebug(SDL_Renderer* r, float camX, float camY) const;
    void renderSolid(SDL_Renderer* r, float camX, float camY) const;
    void renderSemiSolid(SDL_Renderer* r, float camX, float camY) const;
    void renderWater(SDL_Renderer* r, float camX, float camY) const;
};
