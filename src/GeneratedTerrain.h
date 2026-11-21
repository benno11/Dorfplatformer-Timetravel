#pragma once
#include "DefaultTiles.h"
#include "TileRendering.h"
#include <array>
#include <map>
#include <vector>
#include <cstdint>

namespace TerrainVariants {
// Eight neighbors: up, down, left, right, upper-left, upper-right, lower-left, lower-right.
inline std::array<int,4> quarters(unsigned mask) {
    const bool u=mask&1,d=mask&2,l=mask&4,r=mask&8,ul=mask&16,ur=mask&32,dl=mask&64,dr=mask&128;
    return {{!u?(l?2:3):(!l?14:(!ul?8:15)),
             !u?(r?1:0):(!r?11:(!ur?9:10)),
             !d?(l?4:5):(!l?16:(!dl?18:17)),
             !d?(r?7:6):(!r?13:(!dr?19:12))}};
}
// Use the original straight grass strip as the common color pattern for every
// grass shape. Alpha, black outlines, and dirt pixels remain untouched.
inline void normalizeGrass(SDL_Surface* surface,int columns,int tiles,int reference) {
    auto pixel=[&](int x,int y){return static_cast<Uint8*>(surface->pixels)+y*surface->pitch+x*4;};
    std::array<std::array<Uint8,4>,32*16> pattern{};
    for(int y=0;y<16;++y)for(int x=0;x<32;++x){
        const auto* source=pixel(reference%columns*32+x,reference/columns*32+y);
        std::copy(source,source+4,pattern[y*32+x].begin());
    }
    const auto green=[](const auto& p){return p[3] && p[1]>p[0] && p[1]>p[2];};
    for(int tile=0;tile<tiles;++tile)for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        auto* target=pixel(tile%columns*32+x,tile/columns*32+y);
        if(!green(target))continue;
        // Edge tips can extend below the straight strip: continue its nearest
        // green color rather than introducing brown gaps into the grass shape.
        for(int row=std::min(y,15);row>=0;--row){
            const auto& color=pattern[row*32+x];
            if(green(color)){std::copy(color.begin(),color.begin()+3,target);break;}
        }
    }
}
// Side and bottom pieces must share the same soil phase as enclosed blocks.
// Their source images contain edge-specific dirt patterns; keep their contours,
// but use the common original interior for the brown fill everywhere.
inline void normalizeSoil(SDL_Surface* surface,int columns,int tiles,int reference) {
    auto pixel=[&](int x,int y){return static_cast<Uint8*>(surface->pixels)+y*surface->pitch+x*4;};
    std::array<std::array<Uint8,4>,32*32> pattern{};
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        const auto* source=pixel(reference%columns*32+x,reference/columns*32+y);
        std::copy(source,source+4,pattern[y*32+x].begin());
    }
    for(int tile=0;tile<tiles;++tile)for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        auto* target=pixel(tile%columns*32+x,tile/columns*32+y);
        if(target[3] && target[0]>target[1] && target[0]>target[2])
            std::copy(pattern[y*32+x].begin(),pattern[y*32+x].begin()+3,target);
    }
}
template<class Occupied> unsigned mask(int x,int y,Occupied occupied) {
    const int offsets[8][2]={{0,-1},{0,1},{-1,0},{1,0},{-1,-1},{1,-1},{-1,1},{1,1}};
    unsigned result=0;
    for(unsigned i=0;i<8;++i)if(occupied(x+offsets[i][0],y+offsets[i][1]))result|=1u<<i;
    return result;
}
}

class GeneratedTerrain {
    SDL_Texture* source_=nullptr;
    SDL_Texture* atlas_=nullptr;
    std::array<int,256> lookup_{};
    int columns_=1;
    bool needsBuild_=true;
public:
    GeneratedTerrain()=default;
    GeneratedTerrain(const GeneratedTerrain&)=delete;
    GeneratedTerrain& operator=(const GeneratedTerrain&)=delete;
    ~GeneratedTerrain(){reset();}
    void reset(){if(atlas_)SDL_DestroyTexture(atlas_);if(source_)SDL_DestroyTexture(source_);atlas_=source_=nullptr;needsBuild_=true;}
    bool ready()const{return atlas_!=nullptr;}
    bool needsBuild()const{return needsBuild_;}
    bool regenerate(SDL_Renderer* renderer,const std::string& /*level*/) {
        needsBuild_=false;
        if(atlas_){SDL_DestroyTexture(atlas_);atlas_=nullptr;}
        if(!source_)source_=loadTextureSafe(renderer,"assets/Sheets/DF_DefaultTerrain.png");
        if(!source_)return false;
        float sourceW=0,sourceH=0;
        if(!SDL_GetTextureSize(source_,&sourceW,&sourceH) || sourceW!=96 || sourceH!=48)return false;
        std::map<std::array<int,4>,int> unique;
        std::vector<std::array<int,4>> layouts;
        for(unsigned mask=0;mask<256;++mask){
            auto q=TerrainVariants::quarters(mask);
            auto inserted=unique.emplace(q,(int)layouts.size());
            if(inserted.second)layouts.push_back(q);
            lookup_[mask]=inserted.first->second;
        }
        columns_=std::max(1,(int)std::ceil(std::sqrt((double)layouts.size())));
        const int rows=((int)layouts.size()+columns_-1)/columns_;
        SDL_Texture* generated=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,columns_*32,rows*32);
        if(!generated)return false;
        SDL_Texture* target=SDL_GetRenderTarget(renderer);
        float sx=1,sy=1;SDL_GetRenderScale(renderer,&sx,&sy);
        SDL_Rect viewport{},clip{};SDL_GetRenderViewport(renderer,&viewport);SDL_GetRenderClipRect(renderer,&clip);
        const bool viewportSet=SDL_RenderViewportSet(renderer),clipped=SDL_RenderClipEnabled(renderer);
        int lw=0,lh=0;SDL_RendererLogicalPresentation logical{};
        SDL_GetRenderLogicalPresentation(renderer,&lw,&lh,&logical);
        Uint8 red,green,blue,alpha;SDL_GetRenderDrawColor(renderer,&red,&green,&blue,&alpha);
        SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(renderer,&blend);
        if(!SDL_SetRenderTarget(renderer,generated)){SDL_DestroyTexture(generated);return false;}
        SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);
        SDL_SetTextureBlendMode(source_,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(source_,SDL_SCALEMODE_NEAREST);
        bool ok=true;
        for(int layout=0;layout<(int)layouts.size();++layout){
            const int index=layout;
            for(int corner=0;corner<4;++corner){
                const int shape=layouts[layout][corner];
                const int basePiece=shape>=18 ? (shape==18?17:12) : shape;
                const SDL_FRect baseSrc{(float)(basePiece%6*16),(float)(basePiece/6*16),16,16};
                const int tileX=index%columns_*32,tileY=index/columns_*32;
                const SDL_FRect dst{(float)(tileX+corner%2*16),(float)(tileY+corner/2*16),16,16};
                ok=SDL_RenderTexture(renderer,source_,&baseSrc,&dst) && ok;
                if(shape==18 || shape==19) {
                    // Missing lower diagonal: align the small dirt inside corner
                    // with the hole, without placing grass upside down.
                    const bool right=shape==19;
                    const float edge=right ? 14.0f : 0.0f;
                    SDL_SetRenderDrawColor(renderer,0,0,0,255);
                    const SDL_FRect above{dst.x+edge,dst.y+12,2,2};
                    const SDL_FRect side{dst.x+(right?12.0f:2.0f),dst.y+14,2,2};
                    SDL_RenderFillRect(renderer,&above);SDL_RenderFillRect(renderer,&side);
                    SDL_SetRenderDrawColor(renderer,0,0,0,0);
                    const SDL_FRect cutout{dst.x+edge,dst.y+14,2,2};SDL_RenderFillRect(renderer,&cutout);
                }
            }
        }
        SDL_Surface* readback=ok?SDL_RenderReadPixels(renderer,nullptr):nullptr;
        SDL_Surface* pixels=readback?SDL_ConvertSurface(readback,SDL_PIXELFORMAT_RGBA32):nullptr;
        if(readback)SDL_DestroySurface(readback);
        SDL_Texture* normalized=nullptr;
        if(pixels){
            TerrainVariants::normalizeGrass(pixels,columns_,(int)layouts.size(),lookup_[12]);
            TerrainVariants::normalizeSoil(pixels,columns_,(int)layouts.size(),lookup_[255]);
            normalized=SDL_CreateTextureFromSurface(renderer,pixels);
            SDL_DestroySurface(pixels);
        }
        SDL_SetRenderTarget(renderer,target);
        SDL_SetRenderLogicalPresentation(renderer,lw,lh,logical);SDL_SetRenderScale(renderer,sx,sy);
        SDL_SetRenderViewport(renderer,viewportSet?&viewport:nullptr);SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);
        SDL_SetRenderDrawColor(renderer,red,green,blue,alpha);SDL_SetRenderDrawBlendMode(renderer,blend);
        SDL_DestroyTexture(generated);
        if(!ok || !normalized){if(normalized)SDL_DestroyTexture(normalized);return false;}
        atlas_=normalized;SDL_SetTextureBlendMode(atlas_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(atlas_,SDL_SCALEMODE_NEAREST);
        return true;
    }
    template<class Occupied>
    bool draw(SDL_Renderer* renderer,unsigned short id,int x,int y,const SDL_FRect& dst,Occupied occupied)const{
        if(!atlas_ || !DefaultTiles::contains(id))return false;
        const int index=lookup_[TerrainVariants::mask(x,y,occupied)];
        renderTileFrame(renderer,atlas_,Frame{{index%columns_*32,index/columns_*32,32,32},false},dst);
        return true;
    }
};
