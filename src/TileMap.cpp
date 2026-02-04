#include "TileMap.h"

TileMap::TileMap() {
    resize(w,h);
    for(int x=0;x<w;x++) setSolid(x,h-1,1);
}

void TileMap::resize(int W,int H){
    w=W; h=H;
    solid.assign(w*h,0);
    semisolid.assign(w*h,0);
    water.assign(w*h,0);
    bg.assign(w*h,0);
}

unsigned char TileMap::getSolid(int x,int y) const{
    if(x<0||y<0||x>=w||y>=h) return 1;
    return solid[y*w+x];
}

unsigned char TileMap::getSemiSolid(int x,int y) const{
    if(x<0||y<0||x>=w||y>=h) return 0;
    return semisolid[y*w+x];
}

unsigned char TileMap::getWater(int x,int y) const{
    if(x<0||y<0||x>=w||y>=h) return 0;
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
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        if(bg[y*w+x]==0) continue;
        SDL_SetRenderDrawColor(r,30,30,40,255);
        SDL_FRect rc{ x*tileSize-camX, y*tileSize-camY, (float)tileSize, (float)tileSize };
        SDL_RenderFillRectF(r,&rc);
    }
}

void TileMap::renderSolid(SDL_Renderer* r,float camX,float camY) const{
    SDL_SetRenderDrawColor(r,60,60,70,255);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        if(!solid[y*w+x]) continue;
        SDL_FRect rc{ x*tileSize-camX, y*tileSize-camY, (float)tileSize, (float)tileSize };
        SDL_RenderFillRectF(r,&rc);
    }
}

void TileMap::renderSemiSolid(SDL_Renderer* r,float camX,float camY) const{
    SDL_SetRenderDrawColor(r,90,110,150,255);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        if(!semisolid[y*w+x]) continue;
        SDL_FRect rc{ x*tileSize-camX, y*tileSize-camY, (float)tileSize, (float)tileSize };
        SDL_RenderFillRectF(r,&rc);
    }
}

void TileMap::renderWater(SDL_Renderer* r,float camX,float camY) const{
    SDL_SetRenderDrawColor(r,40,90,160,255);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        if(!water[y*w+x]) continue;
        SDL_FRect rc{ x*tileSize-camX, y*tileSize-camY, (float)tileSize, (float)tileSize };
        SDL_RenderFillRectF(r,&rc);
    }
}
