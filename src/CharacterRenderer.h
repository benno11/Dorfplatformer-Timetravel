#pragma once

#include "CharacterAppearance.h"
#include "CharacterPartPhysics.h"
#include "CharacterTextures.h"
#include <SDL3/SDL.h>
#include <cmath>

namespace CharacterCreator {

// Draw parts on a 16 x 28 pixel grid; gameplay continues to use the normal physics.
inline void render(SDL_Renderer* ren, Appearance a, const SDL_FRect& dst,
    Pose pose = Pose::Idle, float time = 0.0f, int facing = 1, Uint8 alpha = 255,
    const PartPhysics* physics = nullptr) {
    a.normalize();
    SDL_BlendMode oldBlend;
    Uint8 oldR, oldG, oldB, oldA;
    SDL_GetRenderDrawBlendMode(ren, &oldBlend);
    SDL_GetRenderDrawColor(ren, &oldR, &oldG, &oldB, &oldA);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const Color outline{28, 29, 43, ""};
    const Color white{255, 249, 229, ""};
    const Color skin = skinColors[a.skin], hair = colors[a.hairColor];
    const Color shirt = colors[a.shirt], pants = colors[a.pants], accent = colors[a.accent];
    if (!a.sprites.base.empty()) {
        int frame = 0;
        switch (pose) {
            case Pose::Run: frame = 1 + (int)(time * 10) % 4; break;
            case Pose::Jump: frame = 5; break;
            case Pose::Fall: frame = 6; break;
            case Pose::Slide: frame = 7; break;
            case Pose::Skid: frame = 8; break;
            case Pose::Hurt: frame = 9; break;
            case Pose::Death: frame = 10; break;
            default: break;
        }
        const float widthScale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
        for (int layer = 0; layer < 2; ++layer) {
            const auto& pixels = layer == 0 ? a.sprites.base : a.sprites.bangs;
            const float swayX = layer && physics ? physics->headX.position + physics->hair.position * 0.3f : 0;
            const float swayY = layer && physics ? physics->headY.position * 0.3f : 0;
            for (int y = 0; y < 28; ++y) for (int x = 0; x < 16; ++x) {
                auto color = rgbaPixel(pixels, y * 176 + frame * 16 + x);
                if (!color.a) continue;
                const Part part = layer ? Part::Hair : (y < 13 ? Part::Skin : (y >= 21 ? Part::Pants :
                    (y >= 19 && x >= 4 && x <= 12 ? Part::Accent : Part::Shirt)));
                if (a.textures[(int)part]) {
                    const auto texture = texturePixel(a, part, {color.r, color.g, color.b, ""}, x, y);
                    color.r = texture.r; color.g = texture.g; color.b = texture.b;
                    color.a = (Uint8)(color.a * texture.a / 255);
                }
                float px = 8 + (x + swayX - 8) * widthScale;
                if (facing < 0) px = 16 - px - widthScale;
                const float py = y + swayY;
                const float left = std::max(0.0f, px), top = std::max(0.0f, py);
                const float right = std::min(16.0f, px + widthScale), bottom = std::min(28.0f, py + 1);
                if (right <= left || bottom <= top) continue;
                const float x1 = std::round(dst.x + left * dst.w / 16), y1 = std::round(dst.y + top * dst.h / 28);
                SDL_FRect pixel{x1, y1, std::round(dst.x + right * dst.w / 16) - x1,
                    std::round(dst.y + bottom * dst.h / 28) - y1};
                SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, (Uint8)(alpha * color.a / 255));
                SDL_RenderFillRect(ren, &pixel);
            }
        }
        SDL_SetRenderDrawColor(ren, oldR, oldG, oldB, oldA);
        SDL_SetRenderDrawBlendMode(ren, oldBlend);
        return;
    }
    const int stride = !physics && pose == Pose::Run ? ((int)(time * 12.0f) % 2 ? 1 : -1) : 0;
    const float bob = physics ? physics->headY.position * 0.3f :
        (pose == Pose::Run ? ((int)(time * 24.0f) % 2) : 0);
    auto block = [&](float x, float y, float w, float h, Color color, Part part = Part::None) {
        const float buildScale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
        x = 8.0f + (x - 8.0f) * buildScale;
        w *= buildScale;
        if (facing < 0) x = 16.0f - x - w;
        const float textureOriginX = x, textureOriginY = y;
        // Keep secondary motion inside the sprite, including narrow slide poses.
        const float right = std::min(16.0f, x + w), bottom = std::min(28.0f, y + h);
        x = std::max(0.0f, x);
        y = std::max(0.0f, y);
        w = right - x;
        h = bottom - y;
        if (w <= 0 || h <= 0) return;
        SDL_FRect rect{dst.x + x * dst.w / 16.0f, dst.y + y * dst.h / 28.0f,
            w * dst.w / 16.0f, h * dst.h / 28.0f};
        if (part == Part::None || a.textures[(int)part] == 0) {
            SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, alpha);
            SDL_RenderFillRect(ren, &rect);
        } else {
            for (float py = y; py < bottom; py += 1.0f) for (float px = x; px < right; px += 1.0f) {
                int u = (int)std::floor((px - textureOriginX) / buildScale);
                if (facing < 0) u = (int)std::ceil(w / buildScale) - 1 - u;
                const auto pixel = texturePixel(a, part, color, u, (int)std::floor(py - textureOriginY));
                SDL_SetRenderDrawColor(ren, pixel.r, pixel.g, pixel.b, (Uint8)(alpha * pixel.a / 255));
                const float x1 = std::round(dst.x + px * dst.w / 16), y1 = std::round(dst.y + py * dst.h / 28);
                SDL_FRect cell{x1, y1, std::round(dst.x + std::min(px + 1, right) * dst.w / 16) - x1,
                    std::round(dst.y + std::min(py + 1, bottom) * dst.h / 28) - y1};
                SDL_RenderFillRect(ren, &cell);
            }
        }
    };
    if (pose == Pose::Death) {
        block(1, 18, 14, 7, outline);
        block(2, 19, 5, 5, skin, Part::Skin);
        block(7, 19, 5, 5, shirt, Part::Shirt);
        block(12, 20, 3, 4, pants, Part::Pants);
        block(4, 20, 1, 1, outline);
    } else {
        // A slide leans forward; its destination is shortened and foot-anchored by the caller.
        const float lean = (pose == Pose::Slide ? 2.0f : 0.0f) + (physics ? physics->headX.position : 0.0f);
        const float headY = 3 + bob + (physics ? physics->headY.position : 0.0f);
        block(3 + lean, headY, 10, 10, outline);
        block(4 + lean, headY + 1, 8, 8, skin, Part::Skin);
        if (a.hairStyle != 3) {
            block(3 + lean, headY, 10, a.hairStyle == 2 ? 4 : 3, hair, Part::Hair);
            block(3 + lean, headY + 3, a.hairStyle == 2 ? 3 : 2, 3, hair, Part::Hair);
            if (a.hairStyle == 2) {
                // Overlapping locks keep long hair attached while its tips lag behind.
                for (int lock = 0; lock < 3; ++lock) {
                    const float sway = physics ? physics->hair.position * (lock + 1) / 3.0f : 0.0f;
                    block(3 + lean + sway, headY + 5 + lock * 2, 3, 3, hair, Part::Hair);
                }
            }
            if (a.hairStyle == 1) {
                const float sway = physics ? physics->hair.position * 0.3f : 0.0f;
                block(4 + lean + sway, headY - 2, 2, 3, hair, Part::Hair);
                block(8 + lean + sway, headY - 1, 2, 2, hair, Part::Hair);
            }
        }
        block(9 + lean, headY + 4, 3, 3, white);
        block(11 + lean, headY + 5, 1, 2, outline);
        block(10 + lean, headY + 8, 2, 1, outline);
        block(3, 13 + bob, 10, 9, outline);
        block(4, 14 + bob, 8, 6, shirt, Part::Shirt);
        block(4, 19 + bob, 8, 2, accent, Part::Accent);
        if (physics) {
            auto limb = [&](float x, float y, float angle, int length, Color cloth, bool hand) {
                for (int pass = 0; pass < 2; ++pass) {
                    for (int segment = 0; segment < length; ++segment) {
                        const float px = x + std::sin(angle) * segment;
                        const float py = y + std::cos(angle) * segment;
                        if (pass == 0) block(px - 1.5f, py, 3, 2, outline);
                        else {
                            const bool isHand = hand && segment >= length - 2;
                            block(px - 0.5f, py, 1, 1, isHand ? skin : cloth,
                                isHand ? Part::Skin : (hand ? Part::Shirt : Part::Pants));
                        }
                    }
                }
            };
            const float lift = pose == Pose::Jump ? 2.2f : (pose == Pose::Slide ? 0.55f : 0.0f);
            limb(3, 15 + bob, physics->arms.position + lift, 6, shirt, true);
            limb(13, 15 + bob, -physics->arms.position - lift, 6, shirt, true);
            const bool airborne = pose == Pose::Jump || pose == Pose::Fall;
            limb(6, 21, physics->legs.position + (airborne ? 0.5f : 0), airborne ? 4 : 6, pants, false);
            limb(11, 21, -physics->legs.position - (airborne ? 0.5f : 0), airborne ? 4 : 6, pants, false);
        } else {
            const float armY = pose == Pose::Jump ? 9.0f : 15 + bob + stride;
            block(1, armY, 3, 6, outline);
            block(2, armY + 1, 2, 3, shirt, Part::Shirt);
            block(2, armY + 4, 2, 2, skin, Part::Skin);
            block(12, armY - stride * 2, 3, 6, outline);
            block(12, armY + 1 - stride * 2, 2, 3, shirt, Part::Shirt);
            block(12, armY + 4 - stride * 2, 2, 2, skin, Part::Skin);
            const int bend = pose == Pose::Jump || pose == Pose::Fall ? 2 : 0;
            block(4, 21, 4, 6 - bend - std::max(0, stride), outline);
            block(5, 21, 2, 4 - bend - std::max(0, stride), pants, Part::Pants);
            block(9, 21, 4, 6 - bend - std::max(0, -stride), outline);
            block(10, 21, 2, 4 - bend - std::max(0, -stride), pants, Part::Pants);
        }
        const float hatLean = lean + (physics ? physics->hatX.position : 0.0f);
        const float hatY = headY + (physics ? physics->hatY.position : 0.0f);
        if (a.hat == 1) {
            block(3 + hatLean, hatY - 1, 10, 3, accent, Part::Hat);
            block(10 + hatLean, hatY + 1, 4, 1, accent, Part::Hat);
        } else if (a.hat == 2) {
            block(4 + hatLean, hatY - 2, 8, 4, accent, Part::Hat);
            block(3 + hatLean, hatY + 1, 10, 2, outline);
        } else if (a.hat == 3) {
            block(3 + hatLean, hatY - 1, 10, 3, accent, Part::Hat);
            for (int x = 3; x <= 11; x += 4) block(x + hatLean, hatY - 3, 2, 3, accent, Part::Hat);
        }
        if (pose == Pose::Hurt) block(9 + lean, headY + 5, 3, 1, outline);
    }
    SDL_SetRenderDrawColor(ren, oldR, oldG, oldB, oldA);
    SDL_SetRenderDrawBlendMode(ren, oldBlend);
}
} // namespace CharacterCreator
