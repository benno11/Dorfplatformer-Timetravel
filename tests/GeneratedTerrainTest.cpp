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
    require(SDL_Init(0),"SDL init");
    {
        SDL_Surface* grass=SDL_CreateSurface(64,32,SDL_PIXELFORMAT_RGBA32);
        auto* bytes=static_cast<Uint8*>(grass->pixels);
        const auto put=[&](int x,int y,SDL_Color c){auto* p=bytes+y*grass->pitch+x*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=c.a;};
        for(int y=0;y<32;++y)for(int x=0;x<64;++x)put(x,y,{80,40,20,255});
        for(int y=0;y<8;++y)for(int x=0;x<32;++x){put(x,y,{20,(Uint8)(80+x),10,255});put(x+32,y,{40,150,30,255});}
        put(34,2,{0,0,0,255});put(35,2,{0,0,0,0});put(36,2,{80,40,20,255});
        TerrainVariants::normalizeGrass(grass,2,2,0);
        for(int y=0;y<8;++y)for(int x=0;x<32;++x){
            if(y==2 && x>=2 && x<=4)continue;
            const auto* a=bytes+y*grass->pitch+x*4;const auto* b=a+32*4;
            require(a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3],"Grass shapes must use one repeating fill");
        }
        const auto* outline=bytes+2*grass->pitch+34*4;
        require(outline[0]==0 && outline[3]==255 && outline[7]==0 && outline[8]==80,"Grass normalization must preserve outlines, cutouts and dirt");
        SDL_DestroySurface(grass);
    }
    {
        SDL_Surface* soil=SDL_CreateSurface(64,32,SDL_PIXELFORMAT_RGBA32);
        auto* bytes=static_cast<Uint8*>(soil->pixels);
        const auto put=[&](int x,int y,SDL_Color c){auto* p=bytes+y*soil->pitch+x*4;p[0]=c.r;p[1]=c.g;p[2]=c.b;p[3]=c.a;};
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){put(x,y,{(Uint8)(100+x),(Uint8)(30+y),15,255});put(x+32,y,{180,60,20,255});}
        put(33,5,{0,0,0,255});put(34,5,{0,0,0,0});put(35,5,{20,100,10,255});
        TerrainVariants::normalizeSoil(soil,2,2,0);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            if(y==5 && x>=1 && x<=3)continue;
            const auto* a=bytes+y*soil->pitch+x*4;const auto* b=a+32*4;
            require(a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3],"Side and bottom soil must share the interior pattern phase");
        }
        const auto* edge=bytes+5*soil->pitch+33*4;
        require(edge[0]==0 && edge[3]==255 && edge[7]==0 && edge[8]==20 && edge[9]==100,
                "Soil normalization must preserve outlines, cutouts and grass");
        SDL_DestroySurface(soil);
    }
    SDL_Surface* output=SDL_CreateSurface(256,64,SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(output);require(renderer,"Renderer");
    {
        GeneratedTerrain terrain;
        SDL_SetRenderScale(renderer,1.5f,1.5f);SDL_Rect clip{2,3,40,20};SDL_SetRenderClipRect(renderer,&clip);
        require(terrain.regenerate(renderer,"level"),"Generate actual texture atlas");
        float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);require(sx==1.5f && sy==1.5f,"Restore scale");
        SDL_Rect restored;SDL_GetRenderClipRect(renderer,&restored);require(restored.x==2 && restored.y==3 && restored.w==40,"Restore clipping");
        SDL_Surface* first=draw(renderer,terrain);require(first,"Read first atlas");
        // Whole interiors must match, not merely the border pixels. Mirrored
        // soil patterns can pass seam checks while creating inconsistent blocks.
        for(int tile=1;tile<8;++tile)for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            Uint8 r,g,b,a,r2,g2,b2,a2;
            SDL_ReadSurfacePixel(first,x,y,&r,&g,&b,&a);
            SDL_ReadSurfacePixel(first,tile*32+x,y,&r2,&g2,&b2,&a2);
            require(r==r2 && g==g2 && b==b2 && a==a2,"Adjacent enclosed blocks must have identical original soil interiors");
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
        std::cout<<layouts.size()<<" neighbor layouts, consistent original interiors, deterministic regeneration and render-state restoration passed\n";
    }
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(output);SDL_Quit();
}
