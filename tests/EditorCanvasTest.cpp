#include "EditorCanvas.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
static void require(bool ok){if(!ok)throw std::runtime_error("Editor coordinate/state regression");}
int main(){
 SDL_setenv_unsafe("SDL_VIDEODRIVER","dummy",1);
 require(SDL_Init(SDL_INIT_VIDEO));
 SDL_Window* window=SDL_CreateWindow("Editor canvas test",640,360,SDL_WINDOW_HIDDEN|SDL_WINDOW_HIGH_PIXEL_DENSITY);
 SDL_Renderer* renderer=SDL_CreateRenderer(window,"software");require(renderer);
 SDL_Texture* previous=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,128,128);
 require(previous);SDL_SetRenderTarget(renderer,previous);SDL_SetRenderScale(renderer,2,2);
 SDL_Rect viewport{4,6,80,80},clip{8,10,40,40};
 SDL_SetRenderViewport(renderer,&viewport);SDL_SetRenderClipRect(renderer,&clip);
 {
  EditorCanvas canvas(renderer);
  require(SDL_GetRenderTarget(renderer)==nullptr && !SDL_RenderClipEnabled(renderer));
  float sx=0,sy=0;SDL_GetRenderScale(renderer,&sx,&sy);require(sx==1 && sy==1);
  int w=0,h=0,pw=0,ph=0;SDL_GetWindowSize(window,&w,&h);SDL_GetWindowSizeInPixels(window,&pw,&ph);
  float x=w*0.5f,y=h*0.5f;canvas.mousePoint(x,y);
  require(std::fabs(x-pw*0.5f)<0.01f && std::fabs(y-ph*0.5f)<0.01f);
  SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(window);
  e.button.x=w*0.5f;e.button.y=h*0.5f;canvas.mouseEvent(e);
  require(std::fabs(e.button.x-x)<0.01f && std::fabs(e.button.y-y)<0.01f);
  e.type=SDL_EVENT_FINGER_DOWN;e.tfinger.x=0.5f;e.tfinger.y=0.5f;canvas.mouseEvent(e);
  require(e.tfinger.x==0.5f && e.tfinger.y==0.5f);
 }
 require(SDL_GetRenderTarget(renderer)==previous);
 float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);require(sx==2 && sy==2);
 SDL_Rect restored{};SDL_GetRenderClipRect(renderer,&restored);require(restored.x==clip.x && restored.y==clip.y && restored.w==clip.w);
 SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(previous);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
 std::cout<<"Editor pixel coordinates, input conversion and render state restoration passed\n";
}
