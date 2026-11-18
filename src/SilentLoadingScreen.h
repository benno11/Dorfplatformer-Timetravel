#pragma once
#include "TextRenderer.h"
#include <SDL3/SDL.h>
#include <string>

inline void DrawSilentLoadingScreen(SDL_Renderer* renderer, const std::string& label) {
    CancelNativeTextOverlay(renderer);
    Uint8 r=0, g=0, b=0, a=255; SDL_BlendMode blend{};
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a); SDL_GetRenderDrawBlendMode(renderer, &blend);
    SDL_Texture* target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    float sx=1, sy=1; int logicalW=0, logicalH=0;
    SDL_RendererLogicalPresentation presentation{};
    SDL_Rect viewport{}, clip{};
    const bool viewportSet = SDL_RenderViewportSet(renderer), clipped = SDL_RenderClipEnabled(renderer);
    SDL_GetRenderScale(renderer, &sx, &sy);
    SDL_GetRenderViewport(renderer, &viewport); SDL_GetRenderClipRect(renderer, &clip);
    SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderViewport(renderer, nullptr); SDL_SetRenderClipRect(renderer, nullptr); SDL_SetRenderScale(renderer, 1, 1);
    int width=0, height=0; SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    SDL_SetRenderDrawColor(renderer, 20, 26, 34, 255); SDL_RenderClear(renderer);
    DrawGameText(renderer, (width - MeasureTextWidth(2,label))/2, height/2-10, 2, label);
    SDL_RenderPresent(renderer);
    SDL_SetRenderLogicalPresentation(renderer, logicalW, logicalH, presentation);
    SDL_SetRenderScale(renderer, sx, sy);
    SDL_SetRenderViewport(renderer, viewportSet ? &viewport : nullptr);
    SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, r, g, b, a); SDL_SetRenderDrawBlendMode(renderer, blend);
}
