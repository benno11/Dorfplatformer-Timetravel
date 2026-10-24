#pragma once

#include "CharacterAppearance.h"
#include "CharacterPartPhysics.h"
#include "CharacterTextures.h"
#include <SDL3/SDL.h>
#include <cmath>

namespace CharacterCreator {

// Match the renderer's sprite clipping, extending only for uncapped procedural hair.
// Use floating-point bounds so very large saved lengths cannot overflow an SDL_Rect.
inline SDL_FRect visualBounds(Appearance a, const SDL_FRect& dst, Pose pose = Pose::Idle,
    float time = 0.0f, const PartPhysics* physics = nullptr) {
    a.normalize();
    if (dst.w <= 0 || dst.h <= 0 || !a.sprites.base.empty() || pose == Pose::Death ||
        !a.uncapHairLength || a.hairStyle != customPartsHairStyle) return dst;
    const float headY = 3 + (physics ? physics->headY.position * 1.3f :
        (pose == Pose::Run ? ((int)(time * 24.0f) % 2) : 0));
    double bottom = 28;
    if (a.hairShape[1] > 0) bottom = std::max(bottom, (double)headY + a.hairShape[0] + a.hairShape[1]);
    if (a.hairShape[2] > 0) bottom = std::max(bottom, (double)headY + 3 + a.hairShape[2]);
    float verticalMotion = 0;
    if (physics) {
        for (int i = 0; i < HairPhysics::jointCount; ++i) {
            verticalMotion = std::max(verticalMotion, physics->builtInHair.joints[i].y - i * physics->builtInHair.restSpacing);
            verticalMotion = std::max(verticalMotion, physics->fringeHair.joints[i].y - i * physics->fringeHair.restSpacing);
        }
    }
    if (a.hairShape[1] > 0 || a.hairShape[2] > 0) bottom += verticalMotion + 1;
    float spread = 0;
    if (physics && !physics->hairCollisions.boxes.empty()) {
        const float scale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
        spread = (float)((double)std::max(a.hairShape[1], a.hairShape[2]) + HairPhysics::reach) * scale * dst.w / 16;
    }
    float left = dst.x - spread, right = dst.x + dst.w + spread;
    float top = 0;
    if (physics) for (const auto& strand : physics->floorHair) {
        if (!strand.initialized) continue;
        for (int i = 0; i < strand.count; ++i) {
            left = std::min(left, dst.x + (strand.points[i].x - strand.width * 0.5f - 1) * dst.w / 16);
            right = std::max(right, dst.x + (strand.points[i].x + strand.width * 0.5f + 1) * dst.w / 16);
            top = std::min(top, strand.points[i].y - 1);
            bottom = std::max(bottom, (double)strand.points[i].y + 1);
        }
    }
    return {left, dst.y + top * dst.h / 28, right - left, (float)std::ceil((bottom - top) * dst.h / 28.0)};
}

// Draw parts on a 16 x 28 pixel grid; gameplay continues to use the normal physics.
inline void render(SDL_Renderer* ren, Appearance a, const SDL_FRect& destination,
    Pose pose = Pose::Idle, float time = 0.0f, int facing = 1, Uint8 alpha = 255,
    const PartPhysics* physics = nullptr) {
    a.normalize();
    float pixelScaleX = 1.0f, pixelScaleY = 1.0f;
    SDL_GetRenderScale(ren, &pixelScaleX, &pixelScaleY);
    if (!std::isfinite(pixelScaleX) || pixelScaleX <= 0) pixelScaleX = 1.0f;
    if (!std::isfinite(pixelScaleY) || pixelScaleY <= 0) pixelScaleY = 1.0f;
    // Draw snapped output pixels at unit scale. Dividing a one-pixel width by
    // fractional UI zoom can round it below one when SDL scales it back, hiding
    // entire hair rows/columns. Preserve the viewport and clip in pixel space
    // too: resetting only the scale would shrink them and hide the character.
    struct RestoreRasterState {
        SDL_Renderer* renderer;
        float x, y;
        SDL_Rect viewport{}, clip{};
        bool hasViewport, clipped;
        ~RestoreRasterState() {
            SDL_SetRenderScale(renderer, x, y);
            SDL_SetRenderViewport(renderer, hasViewport ? &viewport : nullptr);
            SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
        }
    } restore{ren, pixelScaleX, pixelScaleY, {}, {}, SDL_RenderViewportSet(ren), SDL_RenderClipEnabled(ren)};
    SDL_GetRenderViewport(ren, &restore.viewport);
    SDL_GetRenderClipRect(ren, &restore.clip);
    const auto outputRect = [&](const SDL_Rect& rect) {
        return SDL_Rect{(int)std::floor(rect.x * pixelScaleX), (int)std::floor(rect.y * pixelScaleY),
            (int)std::ceil(rect.w * pixelScaleX), (int)std::ceil(rect.h * pixelScaleY)};
    };
    SDL_SetRenderScale(ren, 1, 1);
    if (restore.hasViewport) {
        const auto viewport = outputRect(restore.viewport);
        SDL_SetRenderViewport(ren, &viewport);
    }
    if (restore.clipped) {
        const auto clip = outputRect(restore.clip);
        SDL_SetRenderClipRect(ren, &clip);
    }
    auto fillPixelRect = [&](const SDL_FRect& pixel) {
        SDL_RenderFillRect(ren, &pixel);
    };
    const SDL_FRect dst{destination.x * pixelScaleX, destination.y * pixelScaleY,
        destination.w * pixelScaleX, destination.h * pixelScaleY};
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
                fillPixelRect(pixel);
            }
        }
        SDL_SetRenderDrawColor(ren, oldR, oldG, oldB, oldA);
        SDL_SetRenderDrawBlendMode(ren, oldBlend);
        return;
    }
    const int stride = !physics && pose == Pose::Run ? ((int)(time * 12.0f) % 2 ? 1 : -1) : 0;
    const float bob = physics ? physics->headY.position * 0.3f :
        (pose == Pose::Run ? ((int)(time * 24.0f) % 2) : 0);
    int outputWidth = 0, outputHeight = 0;
    // The window can be smaller than the game/preview texture. Clip against
    // the active target so uncapping cannot hide hair beyond the window size.
    SDL_GetCurrentRenderOutputSize(ren, &outputWidth, &outputHeight);
    const float hairLeft = dst.w > 0 ? -dst.x * 16.0f / dst.w : 0;
    const float hairRight = dst.w > 0 ? (outputWidth - dst.x) * 16.0f / dst.w : 16;
    const float hairTop = dst.h > 0 ? -dst.y * 28.0f / dst.h : 0.0f;
    const float hairBottom = dst.h > 0 ? std::max(28.0f, (outputHeight - dst.y) * 28.0f / dst.h) : 28.0f;
    auto block = [&](float x, float y, float w, float h, Color color, Part part = Part::None, int textureY = 0) {
        const float buildScale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
        x = 8.0f + (x - 8.0f) * buildScale;
        w *= buildScale;
        if (facing < 0) x = 16.0f - x - w;
        const float textureOriginX = x, textureOriginY = y;
        // Keep ordinary parts inside the sprite; uncapped hair may reach the screen edge.
        const bool extendedHair = a.uncapHairLength && a.hairStyle == customPartsHairStyle && part == Part::Hair;
        const float right = std::min(extendedHair ? hairRight : 16.0f, x + w), bottom = std::min(extendedHair ? hairBottom : 28.0f, y + h);
        x = std::max(extendedHair ? hairLeft : 0.0f, x);
        y = std::max(extendedHair ? hairTop : 0.0f, y);
        w = right - x;
        h = bottom - y;
        if (w <= 0 || h <= 0) return;
        SDL_FRect rect{dst.x + x * dst.w / 16.0f, dst.y + y * dst.h / 28.0f,
            w * dst.w / 16.0f, h * dst.h / 28.0f};
        const float pixelRight = std::round(rect.x + rect.w), pixelBottom = std::round(rect.y + rect.h);
        rect.x = std::round(rect.x); rect.y = std::round(rect.y);
        rect.w = pixelRight - rect.x; rect.h = pixelBottom - rect.y;
        const bool clipHair = physics && part == Part::Hair && a.hairStyle != customHairStyle &&
            physics->hairCollisions.blocksWorld(x, y, w, h);
        if (!clipHair && (part == Part::None || a.textures[(int)part] == 0)) {
            SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, alpha);
            fillPixelRect(rect);
        } else {
            for (float py = y; py < bottom; py += 1.0f) for (float px = x; px < right; px += 1.0f) {
                if (clipHair && physics->hairCollisions.blocksWorld(px, py, std::min(1.0f, right - px), std::min(1.0f, bottom - py))) continue;
                int u = (int)std::floor((px - textureOriginX) / buildScale);
                if (facing < 0) u = (int)std::ceil(w / buildScale) - 1 - u;
                const auto pixel = texturePixel(a, part, color, u, (int)std::floor(py - textureOriginY) + textureY);
                SDL_SetRenderDrawColor(ren, pixel.r, pixel.g, pixel.b, (Uint8)(alpha * pixel.a / 255));
                const float x1 = std::round(dst.x + px * dst.w / 16), y1 = std::round(dst.y + py * dst.h / 28);
                SDL_FRect cell{x1, y1, std::round(dst.x + std::min(px + 1, right) * dst.w / 16) - x1,
                    std::round(dst.y + std::min(py + 1, bottom) * dst.h / 28) - y1};
                fillPixelRect(cell);
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
        auto hairOffset = [&](float distance) {
            auto offset = physics ? physics->builtInHair.offset(distance) : HairPhysics::Point{};
            offset.x *= facing < 0 ? -1.0f : 1.0f;
            return offset;
        };
        auto hairStrand = [&](float x, float y, int width, int length, float rootDistance = 0.0f, int strandIndex = -1, bool backStrand = false) {
            auto strandOffset = [&](float distance) {
                if (!physics || strandIndex != 0) return hairOffset(distance);
                auto offset = physics->fringeHair.offset(distance);
                offset.x *= facing < 0 ? -1.0f : 1.0f;
                return offset;
            };
            // Account for joint displacement when culling very long or off-screen strands.
            const float margin = HairPhysics::reach * 2 + 2;
            const double bottom = a.uncapHairLength ? hairBottom : 28.0f;
            const float horizontalBudget = a.uncapHairLength && physics ? std::max(0.0f, hairRight - hairLeft) : 0;
            const int rows = (int)std::min((double)length, std::max(0.0, std::ceil(bottom - y + margin + horizontalBudget)));
            const int first = a.uncapHairLength ? (int)std::min((double)rows,
                std::max(0.0, std::floor((double)hairTop - y - margin))) : 0;
            const float scale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
            const float worldWidth = width * scale;
            auto rowWidth = [&](int row) {
                if (!backStrand || length <= 1 || width <= 1) return (float)width;
                // Keep volume near the head, then round off the silhouette toward
                // a visible one-pixel tip. Measure along the authored strand so
                // culling and floor folds cannot change its shape.
                const float progress = (float)row / (length - 1);
                const float taper = std::clamp((progress - 0.45f) / 0.55f, 0.0f, 1.0f);
                return width - (width - 1) * taper * taper * (3 - 2 * taper);
            };
            auto naturalPoint = [&](int row) {
                const auto offset = strandOffset(rootDistance + row);
                float left = 8 + (x + offset.x - 8) * scale;
                if (facing < 0) left = 16 - left - worldWidth;
                return HairPhysics::Point{left + worldWidth * 0.5f, y + row + offset.y + 0.5f};
            };
            const auto* simulated = physics && strandIndex >= 0 && physics->floorHair[strandIndex].initialized ?
                &physics->floorHair[strandIndex] : nullptr;
            float trailDirection = physics && std::fabs(physics->previousVx) > 1 ?
                (physics->previousVx > 0 ? -1.0f : 1.0f) : (facing < 0 ? 1.0f : -1.0f);
            if (simulated) {
                const auto tip = simulated->at(simulated->length), before = simulated->at(std::max(0.0f, simulated->length - 2));
                trailDirection = std::fabs(tip.x - before.x) > 0.01f ? (tip.x < before.x ? -1.0f : 1.0f) : physics->floorTrailDirection;
            }
            auto current = naturalPoint(first);
            if (simulated) {
                current = simulated->at((float)first);
                if (first > simulated->length) current = physics->hairCollisions.drape(current,
                    {current.x, current.y + first - simulated->length}, worldWidth, trailDirection);
            } else if (physics) current = physics->hairCollisions.drape(naturalPoint(0), current, worldWidth, trailDirection);
            for (int row = first; row < rows; ++row) {
                const float localWidth = rowWidth(row);
                const float drawWorldWidth = localWidth * scale;
                const auto offset = strandOffset(rootDistance + row);
                const auto next = strandOffset(rootDistance + row + 1.0f);
                if (physics) {
                    const auto natural = naturalPoint(row), following = naturalPoint(row + 1);
                    HairPhysics::Point delta{following.x - natural.x, following.y - natural.y};
                    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                    // Only the extra draped tail needs length reconstruction. Ordinary
                    // locks retain their simulated compression and stretch when airborne.
                    if (a.uncapHairLength && !simulated && length > normalHairLengthLimit) {
                        if (distance < 0.0001f) delta = {0, 1};
                        else if (distance < 1) { delta.x /= distance; delta.y /= distance; }
                    }
                    const auto end = simulated && row + 1 <= simulated->length ? simulated->at(row + 1.0f) :
                        physics->hairCollisions.drape(current, {current.x + delta.x, current.y + delta.y}, worldWidth, trailDirection);
                    float left = std::min(current.x, end.x) - drawWorldWidth * 0.5f;
                    const float drawWidth = drawWorldWidth + std::fabs(end.x - current.x);
                    if (facing < 0) left = 16 - left - drawWidth;
                    block(8 + (left - 8) / scale, std::min(current.y, end.y) - 0.5f, drawWidth / scale,
                        1.0f + std::fabs(end.y - current.y), hair, Part::Hair, row);
                    if (std::fabs(end.x - current.x) + std::fabs(end.y - current.y) < 0.0001f &&
                        (!simulated || row >= simulated->length)) break;
                    current = end;
                    continue;
                }
                block(x + offset.x + (width - localWidth) * 0.5f, y + row + offset.y, localWidth,
                    std::max(1.0f, 1.0f + next.y - offset.y), hair, Part::Hair, row);
            }
        };
        block(3 + lean, headY, 10, 10, outline);
        block(4 + lean, headY + 1, 8, 8, skin, Part::Skin);
        if (a.hairStyle == customHairStyle) {
            // A 16x16 transparent image covers the head and trailing locks.
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                const auto pixel = rgbaPixel(a.hairPixels, y * 16 + x);
                if (!pixel.a) continue;
                const auto bend = hairOffset((float)std::max(0, y - 5));
                const Uint8 savedAlpha = alpha;
                alpha = (Uint8)(alpha * pixel.a / 255);
                block(x + lean + bend.x, headY - 3 + y + bend.y, 1, 1, {pixel.r, pixel.g, pixel.b, ""});
                alpha = savedAlpha;
            }
        } else if (a.hairStyle == customPartsHairStyle) {
            block(3 + lean, headY, 10, a.hairShape[0], hair, Part::Hair);
            hairStrand(3 + lean, headY + a.hairShape[0], a.hairShape[3], a.hairShape[1], 0, 0);
            hairStrand(3 + lean, headY + 3, a.hairShape[3], a.hairShape[2], 0, 1, true);
            if (a.hairShape[4]) {
                const auto offset = hairOffset((float)a.hairShape[4]);
                block(4 + lean + offset.x, headY - a.hairShape[4] + offset.y, 2, a.hairShape[4] + 1, hair, Part::Hair);
                block(8 + lean + offset.x * 0.7f, headY - a.hairShape[4] + 1 + offset.y * 0.7f, 2, a.hairShape[4], hair, Part::Hair);
            }
        } else if (a.hairStyle != 3) {
            block(3 + lean, headY, 10, a.hairStyle == 2 ? 4 : 3, hair, Part::Hair);
            hairStrand(3 + lean, headY + 3, a.hairStyle == 2 ? 3 : 2, 3, 0, 0);
            if (a.hairStyle == 2) {
                hairStrand(3 + lean, headY + 5, 3, 7, 2, -1, true);
            }
            if (a.hairStyle == 1) {
                const auto offset = hairOffset(2);
                block(4 + lean + offset.x, headY - 2 + offset.y, 2, 3, hair, Part::Hair);
                block(8 + lean + offset.x * 0.7f, headY - 1 + offset.y * 0.7f, 2, 2, hair, Part::Hair);
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
