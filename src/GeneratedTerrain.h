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
    const bool u=mask&1,d=mask&2,l=mask&4,r=mask&8,ul=mask&16,ur=mask&32;
    return {{!u?(l?2:3):(!l?14:(!ul?8:15)),
             !u?(r?1:0):(!r?11:(!ur?9:10)),
             !d?(l?4:5):(!l?16:17),
             !d?(r?7:6):(!r?13:12)}};
}
inline uint32_t seed(const std::string& level) {
    uint32_t value=2166136261u;
    for(unsigned char c:level){value^=c;value*=16777619u;}
    return value;
}
inline unsigned style(uint32_t level,int x,int y) {
    uint32_t h=level ^ uint32_t(x)*73856093u ^ uint32_t(y)*19349663u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;
    return h%4;
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
    uint32_t seed_=0;
    bool needsBuild_=true;
public:
    GeneratedTerrain()=default;
    GeneratedTerrain(const GeneratedTerrain&)=delete;
    GeneratedTerrain& operator=(const GeneratedTerrain&)=delete;
    ~GeneratedTerrain(){reset();}
    void reset(){if(atlas_)SDL_DestroyTexture(atlas_);if(source_)SDL_DestroyTexture(source_);atlas_=source_=nullptr;needsBuild_=true;}
    bool ready()const{return atlas_!=nullptr;}
    bool needsBuild()const{return needsBuild_;}
    bool regenerate(SDL_Renderer* renderer,const std::string& level) {
        needsBuild_=false;
        seed_=TerrainVariants::seed(level);
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
        columns_=std::max(1,(int)std::ceil(std::sqrt(layouts.size()*4.0)));
        const int rows=((int)layouts.size()*4+columns_-1)/columns_;
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
        for(int layout=0;layout<(int)layouts.size();++layout)for(int variant=0;variant<4;++variant){
            const int index=layout*4+variant;
            for(int corner=0;corner<4;++corner){
                int piece=layouts[layout][corner];
                const bool interior=piece==10 || piece==12 || piece==15 || piece==17;
                if(interior && variant>=2){
                    if(piece==10)piece=15;else if(piece==15)piece=10;else if(piece==12)piece=17;else piece=12;
                }
                const SDL_FRect src{(float)(piece%6*16),(float)(piece/6*16),16,16};
                const SDL_FRect dst{(float)(index%columns_*32+corner%2*16),(float)(index/columns_*32+corner/2*16),16,16};
                const auto flip=interior && variant%2 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                ok=SDL_RenderTextureRotated(renderer,source_,&src,&dst,0,nullptr,flip) && ok;
            }
        }
        SDL_SetRenderTarget(renderer,target);
        SDL_SetRenderLogicalPresentation(renderer,lw,lh,logical);SDL_SetRenderScale(renderer,sx,sy);
        SDL_SetRenderViewport(renderer,viewportSet?&viewport:nullptr);SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);
        SDL_SetRenderDrawColor(renderer,red,green,blue,alpha);SDL_SetRenderDrawBlendMode(renderer,blend);
        if(!ok){SDL_DestroyTexture(generated);return false;}
        atlas_=generated;SDL_SetTextureBlendMode(atlas_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(atlas_,SDL_SCALEMODE_NEAREST);
        return true;
    }
    template<class Occupied>
    bool draw(SDL_Renderer* renderer,unsigned short id,int x,int y,const SDL_FRect& dst,Occupied occupied)const{
        if(!atlas_ || !DefaultTiles::contains(id))return false;
        const int index=lookup_[TerrainVariants::mask(x,y,occupied)]*4+TerrainVariants::style(seed_,x,y);
        renderTileFrame(renderer,atlas_,Frame{{index%columns_*32,index/columns_*32,32,32},false},dst);
        return true;
    }
};
