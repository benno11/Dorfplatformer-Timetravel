#include "GeneratedTerrain.h"
#include <set>
#include <stdexcept>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
SDL_Texture* loadTextureSafe(SDL_Renderer* renderer,const std::string&,std::string*) {
    SDL_Surface* surface=SDL_CreateSurface(96,48,SDL_PIXELFORMAT_RGBA8888);
    for(int i=0;i<18;++i)for(int x=0;x<16;++x){
        SDL_Rect column{i%6*16+x,i/6*16,1,16};
        SDL_FillSurfaceRect(surface,&column,SDL_MapSurfaceRGBA(surface,(Uint8)(i*10+x),70,100,255));
    }
    SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);return texture;
}
static SDL_Surface* draw(SDL_Renderer* renderer,GeneratedTerrain& terrain) {
    SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    for(int x=0;x<8;++x)require(terrain.draw(renderer,7,x,0,SDL_FRect{(float)(x*32),0,32,32},
        [](int,int){return true;}),"Draw generated terrain");
    return SDL_RenderReadPixels(renderer,nullptr);
}
int main(){
    std::set<std::array<int,4>> layouts;
    for(unsigned i=0;i<256;++i){auto q=TerrainVariants::quarters(i);layouts.insert(q);for(int p:q)require(p>=0 && p<20,"Quarter indices valid");}
    require(layouts.size()==47,"All 47 canonical neighbor layouts must be generated");
    require(TerrainVariants::quarters(0)==std::array<int,4>{{3,0,5,6}},"Isolated tile gets all four corners");
    require(TerrainVariants::quarters(255)==std::array<int,4>{{15,10,17,12}},"Enclosed tile uses soil interior");
    require(TerrainVariants::quarters(255^16)[0]==8,"Missing upper-left diagonal adds grass transition");
    require(TerrainVariants::quarters(255^32)[1]==9,"Missing upper-right diagonal adds grass transition");
    require(TerrainVariants::quarters(255^64)[2]==18,"Missing lower-left diagonal has a matching inside corner");
    require(TerrainVariants::quarters(255^128)[3]==19,"Missing lower-right diagonal has a matching inside corner");
    require(TerrainVariants::interiorSource(0,1)==10 && TerrainVariants::interiorSource(1,1)==15,
            "Horizontal variation swaps the left/right source quarters before flipping");
    require(TerrainVariants::interiorSource(0,2)==17 && TerrainVariants::interiorSource(2,2)==15,
            "Vertical variation swaps the upper/lower source quarters before flipping");
    std::set<unsigned> styles;for(int x=0;x<32;++x)styles.insert(TerrainVariants::style(TerrainVariants::seed("level"),x,0));
    require(styles.size()==4,"All four styles selectable");
    require(SDL_Init(0),"SDL init");
    SDL_Surface* output=SDL_CreateSurface(256,64,SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(output);require(renderer,"Renderer");
    {
        GeneratedTerrain terrain;
        SDL_SetRenderScale(renderer,1.5f,1.5f);SDL_Rect clip{2,3,40,20};SDL_SetRenderClipRect(renderer,&clip);
        require(terrain.regenerate(renderer,"level"),"Generate actual texture atlas");
        float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);require(sx==1.5f && sy==1.5f,"Restore scale");
        SDL_Rect restored;SDL_GetRenderClipRect(renderer,&restored);require(restored.x==2 && restored.y==3 && restored.w==40,"Restore clipping");
        SDL_Surface* first=draw(renderer,terrain);require(first,"Read first atlas");
        // Different soil styles must keep identical outer seam pixels.
        for(int tile=1;tile<8;++tile)for(int edge=0;edge<4;++edge)for(int along=2;along<30;++along){
            const int x=edge<2?along:(edge==2?1:30);
            const int y=edge<2?(edge==0?1:30):along;
            Uint8 r,g,b,a,r2,g2,b2,a2;
            SDL_ReadSurfacePixel(first,x,y,&r,&g,&b,&a);
            SDL_ReadSurfacePixel(first,tile*32+x,y,&r2,&g2,&b2,&a2);
            require(r==r2 && g==g2 && b==b2 && a==a2,"Soil variations must not shift shared tile seams");
        }
        require(terrain.regenerate(renderer,"level"),"Regenerate on next load");
        SDL_Surface* second=draw(renderer,terrain);require(second,"Read regenerated atlas");
        for(int y=0;y<32;++y)for(int x=0;x<256;++x){
            Uint8 r,g,b,a,r2,g2,b2,a2;SDL_ReadSurfacePixel(first,x,y,&r,&g,&b,&a);SDL_ReadSurfacePixel(second,x,y,&r2,&g2,&b2,&a2);
            require(r==r2 && g==g2 && b==b2 && a==a2,"Same level regenerates identical visible pixels");
        }
        for(bool right : {false,true}) {
            SDL_SetRenderDrawColor(renderer,3,4,5,255);SDL_RenderClear(renderer);
            require(terrain.draw(renderer,7,0,0,SDL_FRect{0,0,32,32},[&](int x,int y){
                return !(y==1 && x==(right?1:-1));
            }),"Draw lower inside corner");
            SDL_Surface* corner=SDL_RenderReadPixels(renderer,nullptr);require(corner,"Read lower inside corner");
            Uint8 r,g,b,a;SDL_ReadSurfacePixel(corner,right?31:0,31,&r,&g,&b,&a);
            require(r==3 && g==4 && b==5,"Inside corner cutout must align with the diagonal hole");
            SDL_DestroySurface(corner);
        }
        SDL_DestroySurface(first);SDL_DestroySurface(second);
        terrain.reset();require(!terrain.ready() && terrain.needsBuild(),"Reset supports GPU recovery");
        require(terrain.regenerate(renderer,"other-level"),"Regenerate after reset");
        std::cout<<layouts.size()<<" neighbor layouts, 4 soil styles, deterministic regeneration and render-state restoration passed\n";
    }
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(output);SDL_Quit();
}
