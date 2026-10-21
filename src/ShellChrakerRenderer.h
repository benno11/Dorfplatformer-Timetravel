#pragma once
#include "ShellChraker.h"
#include "GameSupport.h"

inline void RenderShellChraker(SDL_Renderer* ren, SDL_Texture* texture, const Frame* bodyFrame,
                               const Frame* armFrame, const ShellChraker::Geometry& geometry) {
    const auto& body = geometry.body;
    auto arm = geometry.arm;
    arm.y -= 4.0f;
    const SDL_Rect bodyDst{(int)std::lround(body.x), (int)std::lround(body.y),
        std::max(1, (int)std::lround(body.w)), std::max(1, (int)std::lround(body.h))};
    if (texture && armFrame) {
        // The shaft is hidden on the rear half; drawing the body afterward also
        // covers the portion of the arm that is still inside its shell.
        SDL_Rect previousClip;
        SDL_GetRenderClipRect(ren, &previousClip);
        const bool clipped = SDL_RenderClipEnabled(ren);
        const int center = (int)std::lround(geometry.centerX);
        const int left = (int)std::floor(arm.x);
        const int right = (int)std::ceil(arm.x + arm.w);
        SDL_Rect clip{geometry.facing < 0 ? left : center, (int)std::floor(arm.y),
            geometry.facing < 0 ? std::max(0, center - left) : std::max(0, right - center),
            std::max(1, (int)std::ceil(arm.h) + 1)};
        if (clipped) SDL_GetRectIntersection(&clip, &previousClip, &clip);
        SDL_SetRenderClipRect(ren, &clip);
        const SDL_Rect armDst{(int)std::lround(arm.x), (int)std::lround(arm.y),
            std::max(1, (int)std::lround(arm.w)), std::max(1, (int)std::lround(arm.h))};
        renderFrameEx(ren, texture, *armFrame, armDst, geometry.facing < 0 ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL);
        SDL_SetRenderClipRect(ren, clipped ? &previousClip : nullptr);
    }
    if (texture && bodyFrame) renderFrame(ren, texture, *bodyFrame, bodyDst);
    else {
        SDL_SetRenderDrawColor(ren, 200, 35, 30, 255);
        SDL_RenderFillRect(ren, &bodyDst);
    }
}
