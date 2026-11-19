#pragma once
#include "GameSupport.h"
#include <cmath>

// Cover physical pixel boundaries at fractional zoom. The tiny outward bias
// avoids precision-induced holes when SDL transforms the vertices back to pixels.
// Nearest sampling prevents transparent atlas padding from bleeding into tiles.
inline void renderTileFrame(SDL_Renderer* renderer, SDL_Texture* texture, const Frame& frame, const SDL_FRect& dst) {
    float sx=1, sy=1, tw=0, th=0;
    if (!renderer || !texture || !SDL_GetRenderScale(renderer,&sx,&sy) || sx<=0 || sy<=0 ||
        !SDL_GetTextureSize(texture,&tw,&th) || tw<=0 || th<=0) return;
    const float left=(std::floor(dst.x*sx)-0.01f)/sx, right=(std::ceil((dst.x+dst.w)*sx)+0.01f)/sx;
    const float top=(std::floor(dst.y*sy)-0.01f)/sy, bottom=(std::ceil((dst.y+dst.h)*sy)+0.01f)/sy;
    const float u0=(frame.rect.x+0.5f)/tw, v0=(frame.rect.y+0.5f)/th;
    const float u1=(frame.rect.x+frame.rect.w-0.5f)/tw, v1=(frame.rect.y+frame.rect.h-0.5f)/th;
    SDL_FPoint uv[4]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    if(frame.rotated) { uv[1]={u0,v1}; uv[2]={u1,v1}; uv[3]={u1,v0}; }
    Uint8 r=255,g=255,b=255,a=255;
    SDL_GetTextureColorMod(texture,&r,&g,&b); SDL_GetTextureAlphaMod(texture,&a);
    const SDL_FColor color{r/255.0f,g/255.0f,b/255.0f,a/255.0f};
    const SDL_Vertex vertices[]={{{left,top},color,uv[0]},{{right,top},color,uv[1]},
                                {{right,bottom},color,uv[2]},{{left,bottom},color,uv[3]}};
    const int indices[]={0,1,2,2,3,0};
    SDL_ScaleMode previous=SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(texture,&previous);
    SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);
    SDL_RenderGeometry(renderer,texture,vertices,4,indices,6);
    SDL_SetTextureScaleMode(texture,previous);
}
