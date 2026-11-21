#include "TileRendering.h"
#include <stdexcept>
#include <iostream>
static void require(bool ok) { if(!ok) throw std::runtime_error("Tile seam or atlas bleed"); }
int main() {
 SDL_Surface* surface=SDL_CreateSurface(128,128,SDL_PIXELFORMAT_RGBA8888);
 SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface); require(renderer);
 SDL_Surface* atlas=SDL_CreateSurface(12,12,SDL_PIXELFORMAT_RGBA8888);
 SDL_FillSurfaceRect(atlas,nullptr,SDL_MapSurfaceRGBA(atlas,255,0,255,0));
 SDL_Rect src{2,2,8,8}; SDL_FillSurfaceRect(atlas,&src,SDL_MapSurfaceRGBA(atlas,255,0,0,255));
 SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,atlas); require(texture);
 SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
 SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
 for(float zoom:{1.5f,1.999f,2.25f,3.0f}) for(bool rotated:{false,true}) {
  SDL_SetRenderScale(renderer,1,1); SDL_SetRenderDrawColor(renderer,0,255,0,255); SDL_RenderClear(renderer);
  SDL_SetRenderScale(renderer,zoom,zoom);
  for(int y=-1;y<12;++y) for(int x=-1;x<12;++x)
   renderTileFrame(renderer,texture,Frame{src,rotated},SDL_FRect{x*16.0f-0.7f,y*16.0f-0.3f,16,16});
  float sx=0,sy=0;SDL_GetRenderScale(renderer,&sx,&sy);require(sx==zoom && sy==zoom);
  SDL_RenderPresent(renderer);
  SDL_ScaleMode mode; SDL_GetTextureScaleMode(texture,&mode); require(mode==SDL_SCALEMODE_LINEAR);
  for(int y=1;y<127;++y) for(int x=1;x<127;++x) {
   Uint8 r,g,b,a; require(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
   if (!(r==255 && g==0 && b==0 && a==255)) { std::cerr<<zoom<<" "<<rotated<<" "<<x<<","<<y<<" "<<(int)r<<","<<(int)g<<","<<(int)b<<","<<(int)a<<"\n"; require(false); }
  }
 }
 SDL_DestroyTexture(texture); SDL_DestroySurface(atlas); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
 std::cout<<"Fractional zoom tile seams, rotated atlas frames and sampling restoration passed\n";
}
