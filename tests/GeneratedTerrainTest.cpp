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
    for(unsigned i=0;i<256;++i){auto q=TerrainVariants::quarters(i);layouts.insert(q);for(int p:q)require(p>=0 && p<18,"Quarter indices valid");}
    require(layouts.size()>16,"More topological variants than legacy terrain");
    require(TerrainVariants::quarters(0)==std::array<int,4>{{3,0,5,6}},"Isolated tile gets all four corners");
    require(TerrainVariants::quarters(255)==std::array<int,4>{{15,10,17,12}},"Enclosed tile uses soil interior");
    require(TerrainVariants::quarters(255^16)[0]==8,"Missing upper-left diagonal adds grass transition");
    require(TerrainVariants::quarters(255^32)[1]==9,"Missing upper-right diagonal adds grass transition");
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
        require(terrain.regenerate(renderer,"level"),"Regenerate on next load");
        SDL_Surface* second=draw(renderer,terrain);require(second,"Read regenerated atlas");
        for(int y=0;y<32;++y)for(int x=0;x<256;++x){
            Uint8 r,g,b,a,r2,g2,b2,a2;SDL_ReadSurfacePixel(first,x,y,&r,&g,&b,&a);SDL_ReadSurfacePixel(second,x,y,&r2,&g2,&b2,&a2);
            require(r==r2 && g==g2 && b==b2 && a==a2,"Same level regenerates identical visible pixels");
        }
        SDL_DestroySurface(first);SDL_DestroySurface(second);
        terrain.reset();require(!terrain.ready() && terrain.needsBuild(),"Reset supports GPU recovery");
        require(terrain.regenerate(renderer,"other-level"),"Regenerate after reset");
        std::cout<<layouts.size()<<" neighbor layouts, 4 soil styles, deterministic regeneration and render-state restoration passed\n";
    }
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(output);SDL_Quit();
}
