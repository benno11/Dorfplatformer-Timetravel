#include "SilentLoadingScreen.h"
#include "IosAppSleep.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
void CancelNativeTextOverlay(SDL_Renderer*) {}
int MeasureTextWidth(int scale, const std::string& text) { return scale * 6 * static_cast<int>(text.size()); }
void DrawGameText(SDL_Renderer* renderer, int x, int y, int scale, const std::string& text) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    const SDL_FRect rect{(float)x,(float)y,(float)MeasureTextWidth(scale,text),(float)scale*10};
    SDL_RenderFillRect(renderer, &rect);
}
int main() {
    assert(SDL_Init(0));
    auto* surface = SDL_CreateSurface(320,180,SDL_PIXELFORMAT_RGBA8888);
    auto* renderer = SDL_CreateSoftwareRenderer(surface);
    auto* target = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,160,90);
    assert(renderer && target);
    SDL_SetRenderTarget(renderer,target);
    SDL_SetRenderScale(renderer,2,2);
    SDL_Rect viewport{2,3,60,30}, clip{1,1,20,10};
    SDL_SetRenderViewport(renderer,&viewport); SDL_SetRenderClipRect(renderer,&clip);
    SDL_SetRenderDrawColor(renderer,1,2,3,4);
    int readyFrames = 0;
    IosAppSleep::ReadyHook ready([&] { ++readyFrames; });
    DrawSilentLoadingScreen(renderer,"Loading game...");
    assert(readyFrames == 0); // The loading frame cannot release its own mute.
    IosAppSleep::framePresented(); assert(readyFrames == 1);
    assert(SDL_GetRenderTarget(renderer)==target);
    float sx=0,sy=0; SDL_GetRenderScale(renderer,&sx,&sy); assert(sx==2&&sy==2);
    SDL_Rect restored{}; SDL_GetRenderViewport(renderer,&restored);
    assert(restored.x==viewport.x&&restored.y==viewport.y&&restored.w==viewport.w&&restored.h==viewport.h);
    SDL_GetRenderClipRect(renderer,&restored); assert(restored.x==clip.x&&restored.y==clip.y&&restored.w==clip.w&&restored.h==clip.h);
    Uint8 r,g,b,a; SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a); assert(r==1&&g==2&&b==3&&a==4);
    SDL_SetRenderTarget(renderer,nullptr);
    auto* output=SDL_RenderReadPixels(renderer,nullptr); assert(output);
    assert(SDL_ReadSurfacePixel(output,0,0,&r,&g,&b,&a));assert(r==20&&g==26&&b==34);
    assert(SDL_ReadSurfacePixel(output,160,90,&r,&g,&b,&a));assert(r==255&&g==255&&b==255);
    SDL_DestroySurface(output);SDL_DestroyTexture(target);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);SDL_Quit();
}
