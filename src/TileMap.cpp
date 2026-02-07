#include "TileMap.h"
#include <algorithm>
#include <cmath>

static void collectTileRects(
    const std::vector<unsigned char>& layer,
    int w,
    int h,
    int tileSize,
    float camX,
    float camY,
    int viewW,
    int viewH,
    std::vector<SDL_FRect>& out
) {
    int minX = std::max(0, (int)std::floor(camX / tileSize) - 1);
    int maxX = std::min(w - 1, (int)std::floor((camX + (float)viewW) / tileSize) + 1);
    int minY = std::max(0, (int)std::floor(camY / tileSize) - 1);
    int maxY = std::min(h - 1, (int)std::floor((camY + (float)viewH) / tileSize) + 1);
    if (maxX < minX || maxY < minY) return;

    int visibleTiles = (maxX - minX + 1) * (maxY - minY + 1);
    out.reserve(visibleTiles);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!layer[y * w + x]) continue;
            out.push_back(SDL_FRect{
                x * tileSize - camX,
                y * tileSize - camY,
                (float)tileSize,
                (float)tileSize
            });
        }
    }
}

TileMap::TileMap() {
    resize(w,h);
    for(int x=0;x<w;x++) setSolid(x,h-1,1);
}

void TileMap::resize(int W,int H){
    w=W; h=H;
    solid.assign(w*h,0);
    semisolid.assign(w*h,0);
    water.assign(w*h,0);
    tileIds.assign(w*h,0);
    bg.assign(w*h,0);
}

unsigned char TileMap::getSolid(int x,int y) const{
    if(x<0||x>=w) return 1;
    if(y<0) y=0;
    else if(y>=h) y=h-1;
    return solid[y*w+x];
}

unsigned char TileMap::getSemiSolid(int x,int y) const{
    if(x<0||x>=w) return 0;
    if(y<0) y=0;
    else if(y>=h) y=h-1;
    return semisolid[y*w+x];
}

unsigned char TileMap::getWater(int x,int y) const{
    if(x<0||x>=w) return 0;
    if(y<0) y=0;
    else if(y>=h) y=h-1;
    return water[y*w+x];
}

void TileMap::setSolid(int x,int y,unsigned char v){
    if(x<0||y<0||x>=w||y>=h) return;
    solid[y*w+x]=v;
}

void TileMap::setSemiSolid(int x,int y,unsigned char v){
    if(x<0||y<0||x>=w||y>=h) return;
    semisolid[y*w+x]=v;
}

void TileMap::setWater(int x,int y,unsigned char v){
    if(x<0||y<0||x>=w||y>=h) return;
    water[y*w+x]=v;
}

void TileMap::renderBgDebug(SDL_Renderer* r,float camX,float camY) const{
    int viewW = 0, viewH = 0;
    SDL_GetRendererOutputSize(r, &viewW, &viewH);
    if (viewW <= 0 || viewH <= 0) return;

    std::vector<SDL_FRect> rects;
    int minX = std::max(0, (int)std::floor(camX / tileSize) - 1);
    int maxX = std::min(w - 1, (int)std::floor((camX + (float)viewW) / tileSize) + 1);
    int minY = std::max(0, (int)std::floor(camY / tileSize) - 1);
    int maxY = std::min(h - 1, (int)std::floor((camY + (float)viewH) / tileSize) + 1);
    if (maxX < minX || maxY < minY) return;
    rects.reserve((maxX - minX + 1) * (maxY - minY + 1));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (bg[y * w + x] == 0) continue;
            rects.push_back(SDL_FRect{
                x * tileSize - camX,
                y * tileSize - camY,
                (float)tileSize,
                (float)tileSize
            });
        }
    }
    if (rects.empty()) return;
    SDL_SetRenderDrawColor(r,30,30,40,255);
    SDL_RenderFillRectsF(r, rects.data(), (int)rects.size());
}

void TileMap::renderSolid(SDL_Renderer* r,float camX,float camY) const{
    int viewW = 0, viewH = 0;
    SDL_GetRendererOutputSize(r, &viewW, &viewH);
    if (viewW <= 0 || viewH <= 0) return;

    std::vector<SDL_FRect> rects;
    collectTileRects(solid, w, h, tileSize, camX, camY, viewW, viewH, rects);
    if (rects.empty()) return;
    SDL_SetRenderDrawColor(r,60,60,70,255);
    SDL_RenderFillRectsF(r, rects.data(), (int)rects.size());
}

void TileMap::renderSemiSolid(SDL_Renderer* r,float camX,float camY) const{
    int viewW = 0, viewH = 0;
    SDL_GetRendererOutputSize(r, &viewW, &viewH);
    if (viewW <= 0 || viewH <= 0) return;

    std::vector<SDL_FRect> rects;
    collectTileRects(semisolid, w, h, tileSize, camX, camY, viewW, viewH, rects);
    if (rects.empty()) return;
    SDL_SetRenderDrawColor(r,90,110,150,255);
    SDL_RenderFillRectsF(r, rects.data(), (int)rects.size());
}

void TileMap::renderWater(SDL_Renderer* r,float camX,float camY) const{
    int viewW = 0, viewH = 0;
    SDL_GetRendererOutputSize(r, &viewW, &viewH);
    if (viewW <= 0 || viewH <= 0) return;

    std::vector<SDL_FRect> rects;
    collectTileRects(water, w, h, tileSize, camX, camY, viewW, viewH, rects);
    if (rects.empty()) return;
    SDL_SetRenderDrawColor(r,40,90,160,255);
    SDL_RenderFillRectsF(r, rects.data(), (int)rects.size());
}
