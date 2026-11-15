#pragma once
#include "TextRenderer.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace AchievementToast {
inline void roundedFill(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color) {
    radius=std::max(0,std::min({radius,rect.w/2,rect.h/2}));
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    // Scanline rounded corners work with every SDL renderer, without textures.
    for(int row=0;row<rect.h;++row){
        const float distance=std::max(0.0f,std::max(radius-row-0.5f,row+0.5f-(rect.h-radius)));
        const int inset=distance>0 ? static_cast<int>(std::ceil(radius-std::sqrt(std::max(0.0f,radius*radius-distance*distance)))) : 0;
        SDL_RenderLine(renderer,static_cast<float>(rect.x+inset),static_cast<float>(rect.y+row),
                       static_cast<float>(rect.x+rect.w-inset-1),static_cast<float>(rect.y+row));
    }
}
inline void draw(SDL_Renderer* renderer,int canvasW,int canvasH,const std::string& title) {
    if(canvasW<140 || canvasH<104)return;
    Uint8 r,g,b,a;SDL_BlendMode blend;
    SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
    SDL_GetRenderDrawBlendMode(renderer,&blend);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    const int width=std::min(380,canvasW-32);
    const SDL_Rect card{canvasW-width-16,canvasH-96,width,80};
    // Soft layered shadow, a fine neutral border, and a warm white surface.
    for(int spread=6;spread>=1;--spread)
        roundedFill(renderer,{card.x-spread,card.y+3-spread,card.w+2*spread,card.h+2*spread},10+spread,{0,0,0,static_cast<Uint8>(spread==1?14:5)});
    roundedFill(renderer,card,10,{211,216,222,255});
    roundedFill(renderer,{card.x+1,card.y+1,card.w-2,card.h-2},9,{250,251,252,255});
    const SDL_Rect badge{card.x+14,card.y+23,34,34};
    roundedFill(renderer,badge,17,{224,244,231,255});
    SDL_SetRenderDrawColor(renderer,37,133,77,255);
    for(int stroke=0;stroke<3;++stroke){
        SDL_RenderLine(renderer,badge.x+9.0f,badge.y+17.0f+stroke,badge.x+14.0f,badge.y+22.0f+stroke);
        SDL_RenderLine(renderer,badge.x+14.0f,badge.y+22.0f+stroke,badge.x+25.0f,badge.y+11.0f+stroke);
    }
    const int textX=card.x+62, available=card.w-76;
    std::string heading="ACHIEVEMENT UNLOCKED";
    if(MeasureTextWidth(1,heading)>available)heading="UNLOCKED";
    DrawTextColored(renderer,textX,card.y+17,1,heading,{102,112,125,255});
    int scale=MeasureTextWidth(2,title)<=available ? 2 : 1;
    std::string label=title;
    while(!label.empty() && MeasureTextWidth(scale,label)>available){
        // Keep UTF-8 code points intact when fitting narrow windows.
        size_t end=label.size()-1;
        while(end>0 && (static_cast<unsigned char>(label[end])&0xc0)==0x80)--end;
        label.resize(end);
        std::string clipped=label+"...";
        if(MeasureTextWidth(scale,clipped)<=available){label=std::move(clipped);break;}
    }
    DrawTextColored(renderer,textX,card.y+39,scale,label,{32,40,51,255});
    SDL_SetRenderDrawColor(renderer,r,g,b,a);
    SDL_SetRenderDrawBlendMode(renderer,blend);
}
}
