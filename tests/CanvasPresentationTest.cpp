#include "CanvasPresentation.h"
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
#include <stdexcept>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    for (const auto size : {std::array<int, 2>{1920,1080}, {1280,1024}, {2560,1080},
            {1080,1920}, {1365,767}, {320,240}, {1,1}}) {
        const auto rect = CanvasPresentation::destination(size[0],size[1]);
        require(rect.x==0 && rect.y==0 && rect.w==size[0] && rect.h==size[1], "Canvas left output borders");
        int x=-1,y=-1;
        require(CanvasPresentation::toCanvas(0,0,size[0],size[1],960,540,x,y) && x==0 && y==0,
            "Top-left pixel did not map to canvas origin");
        require(CanvasPresentation::toCanvas(size[0]-1,size[1]-1,size[0],size[1],960,540,x,y) &&
            x>=0 && x<960 && y>=0 && y<540, "Bottom-right pixel was excluded or outside canvas");
        require(!CanvasPresentation::toCanvas(size[0],size[1],size[0],size[1],960,540,x,y), "Outside point was accepted");
        require(!CanvasPresentation::toCanvas(-1,0,size[0],size[1],960,540,x,y), "Negative point was accepted");
    }
    int x=0,y=0;
    require(CanvasPresentation::toCanvas(640,512,1280,1024,960,540,x,y) && x==480 && y==270,
        "Independent X/Y scaling did not map the center");
    require(CanvasPresentation::toCanvas(1279,1023,1280,1024,1280,1024,x,y) && x==1279 && y==1023,
        "Native resolution edge pixels changed");
    require(!CanvasPresentation::toCanvas(0,0,0,1080,960,540,x,y), "Zero output width accepted");
    require(!CanvasPresentation::toCanvas(0,0,1920,1080,960,0,x,y), "Zero canvas height accepted");
    const auto invalid=CanvasPresentation::destination(-1,0);
    require(invalid.w==0 && invalid.h==0, "Invalid output size became a negative render rectangle");
    require(SDL_Init(0), "Could not initialize SDL");
    auto* surface = SDL_CreateSurface(1280, 1024, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, "Could not allocate output surface");
    auto* renderer = SDL_CreateSoftwareRenderer(surface);
    require(renderer != nullptr, "Could not create software renderer");
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 960, 540);
    require(texture != nullptr, "Could not allocate canvas texture");
    require(SDL_SetRenderTarget(renderer, texture), "Could not select canvas");
    SDL_SetRenderDrawColor(renderer, 200, 80, 50, 255); SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
    const auto destination = CanvasPresentation::destination(1280, 1024);
    const SDL_FRect target{(float)destination.x,(float)destination.y,(float)destination.w,(float)destination.h};
    require(SDL_RenderTexture(renderer, texture, nullptr, &target), "Could not present stretched canvas");
    SDL_RenderPresent(renderer);
    auto checkEdges = [&]() {
        auto pixel = [&](int px, int py) {
            Uint8 red=0,green=0,blue=0,alpha=0;
            require(SDL_ReadSurfacePixel(surface,px,py,&red,&green,&blue,&alpha), "Could not read output pixel");
            require(red==200 && green==80 && blue==50, "A border pixel was not filled by the canvas");
        };
        for(int px=0;px<1280;++px) { pixel(px,0); pixel(px,1023); }
        for(int py=0;py<1024;++py) { pixel(0,py); pixel(1279,py); }
    };
    checkEdges();
    // Editor and difficulty dialogs use SDL's independent-axis presentation too.
    require(SDL_SetRenderLogicalPresentation(renderer,960,540,SDL_LOGICAL_PRESENTATION_STRETCH), "Could not set stretched logical presentation");
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer,200,80,50,255);
    const SDL_FRect logicalCanvas{0,0,960,540}; SDL_RenderFillRect(renderer,&logicalCanvas);
    SDL_RenderPresent(renderer); checkEdges();
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit();
    std::cout<<"Full-canvas presentation and pointer mapping tests passed\n";
}
