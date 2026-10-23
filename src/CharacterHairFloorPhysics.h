#pragma once
#include "CharacterHairPhysics.h"

namespace CharacterCreator {
// Persistent, bounded rope for floor-length locks. Positions are sprite-relative;
// rebasing through world coordinates keeps grounded hair still as the player moves.
struct HairFloorPhysics {
    using Point = HairPhysics::Point;
    static constexpr int capacity = 64;
    static constexpr float maxLength = 189;
    std::array<Point, capacity> points{}, previous{};
    Point oldOrigin{}, oldScale{1, 1};
    float length = 0, linkLength = 1, width = 1;
    int count = 0;
    bool initialized = false, hadWorldTransform = false;

    Point at(float distance) const {
        const float sample = std::clamp(distance / linkLength, 0.0f, (float)(count - 1));
        const int index = std::min((int)sample, count - 2);
        const float blend = sample - index;
        return {points[index].x + (points[index + 1].x - points[index].x) * blend,
            points[index].y + (points[index + 1].y - points[index].y) * blend};
    }

    void update(float elapsed, Point root, int authoredLength, float strandWidth, float direction,
        const HairPhysics::CollisionScene& scene, float springiness = 1, Point airflow = {}, float verticalStrain = 0) {
        if (elapsed <= 0) return;
        if (authoredLength <= 0 || !std::isfinite(elapsed)) { *this = {}; return; }
        const float newLength = std::min((float)authoredLength, maxLength);
        if (!initialized || newLength != length || strandWidth != width ||
            hadWorldTransform != scene.hasWorldTransform) {
            length = newLength;
            width = strandWidth;
            count = std::min(capacity, std::max(2, (int)std::ceil(length) + 1));
            linkLength = length / (count - 1);
            points[0] = previous[0] = root;
            for (int i = 1; i < count; ++i)
                points[i] = previous[i] = scene.drape(points[i - 1],
                    {points[i - 1].x, points[i - 1].y + linkLength}, width, direction);
            initialized = true;
        } else if (hadWorldTransform) {
            const Point shift{(oldOrigin.x - scene.worldOrigin.x) / scene.worldScale.x,
                (oldOrigin.y - scene.worldOrigin.y) / scene.worldScale.y};
            if (std::fabs(shift.x) + std::fabs(shift.y) > length + 32) {
                initialized = false; // Respawns and wraps must not stretch a rope across the map.
                update(elapsed, root, authoredLength, strandWidth, direction, scene, springiness, airflow, verticalStrain);
                return;
            }
            for (int i = 0; i < count; ++i) {
                for (Point* point : {&points[i], &previous[i]}) {
                    point->x = point->x * oldScale.x / scene.worldScale.x + shift.x;
                    point->y = point->y * oldScale.y / scene.worldScale.y + shift.y;
                }
            }
        }
        oldOrigin = scene.worldOrigin;
        oldScale = scene.worldScale;
        hadWorldTransform = scene.hasWorldTransform;
        float left = root.x, right = root.x, top = root.y, bottom = root.y;
        for (int i = 1; i < count; ++i) {
            left = std::min(left, points[i].x); right = std::max(right, points[i].x);
            top = std::min(top, points[i].y); bottom = std::max(bottom, points[i].y);
        }
        const float margin = linkLength * 4 + width + 8;
        HairPhysics::CollisionScene contacts = scene;
        contacts.boxes.erase(std::remove_if(contacts.boxes.begin(), contacts.boxes.end(), [&](const HairPhysics::Box& box) {
            return box.x > right + margin || box.x + box.w < left - margin ||
                box.y > bottom + margin || box.y + box.h < top - margin;
        }), contacts.boxes.end());
        springiness = std::isfinite(springiness) ? std::clamp(springiness, 0.75f, 1.25f) : 1;
        const float dt = std::min(elapsed, 1.0f / 30);
        const int steps = std::max(1, (int)std::ceil(dt * 120));
        const float h = dt / steps;
        const float elasticLink = linkLength * (1 + (std::isfinite(verticalStrain) ? std::clamp(verticalStrain, -0.25f, 0.18f) : 0));
        for (int step = 0; step < steps; ++step) {
            for (int i = 1; i < count; ++i) {
                const Point current = points[i];
                const float drag = std::exp(-2.5f * h / springiness);
                const float exposure = 0.3f + 0.7f * i / (count - 1);
                const float mass = std::clamp(0.7f + width * 0.15f, 0.5f, 2.0f);
                const float windX = HairPhysics::airDrag(airflow.x, exposure, mass) * springiness;
                const float windY = HairPhysics::airDrag(airflow.y, exposure, mass) * springiness;
                points[i].x += (current.x - previous[i].x) * drag + windX * h * h;
                points[i].y += (current.y - previous[i].y) * drag + (160 + windY) * h * h;
                previous[i] = current;
            }
            for (int iteration = 0; iteration < 6; ++iteration) {
                points[0] = root;
                for (int i = 1; i < count; ++i) {
                    float dx = points[i].x - points[i - 1].x;
                    float dy = points[i].y - points[i - 1].y;
                    float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < 0.0001f) {
                        // A deterministic seed lets surplus length curl at a blocked tip.
                        dx = std::sin(i * 1.7f); dy = -0.5f;
                        distance = std::sqrt(dx * dx + dy * dy);
                    }
                    const float correction = (distance - elasticLink) / distance;
                    const float weight = i == 1 ? 1.0f : 0.5f;
                    points[i].x -= dx * correction * weight;
                    points[i].y -= dy * correction * weight;
                    if (i > 1) {
                        points[i - 1].x += dx * correction * weight;
                        points[i - 1].y += dy * correction * weight;
                    }
                }
                // Keep non-neighboring links from collapsing into the same floor pixel.
                const float separation = std::min(width, 2.0f);
                for (int i = 1; i < count; ++i) for (int j = i + 4; j < count; ++j) {
                    const float dx = points[j].x - points[i].x, dy = points[j].y - points[i].y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < 0.0001f || distance >= separation) continue;
                    const float correction = (separation - distance) / distance * 0.25f;
                    points[i].x -= dx * correction; points[i].y -= dy * correction;
                    points[j].x += dx * correction; points[j].y += dy * correction;
                }
                for (int i = 1; i < count; ++i) {
                    const Point target = points[i];
                    auto resolve = [&](Point from, Point to, bool worldOnly) {
                        const auto result = contacts.resolve({from.x - scene.root.x, from.y - scene.root.y},
                            {to.x - scene.root.x, to.y - scene.root.y}, worldOnly, width * 0.5f, 0.5f);
                        return Point{result.x + scene.root.x, result.y + scene.root.y};
                    };
                    const Point resolved = resolve(points[i - 1], resolve(previous[i], target, false), true);
                    if (resolved.y < target.y - 0.0001f) {
                        // Inelastic floor contact with tangential friction; a turn cannot
                        // instantly drag the entire resting tail to the opposite side.
                        previous[i].x = resolved.x - (resolved.x - previous[i].x) * std::exp(-18 * h);
                        previous[i].y = resolved.y;
                    }
                    if (std::fabs(resolved.x - target.x) > 0.0001f) previous[i].x = resolved.x;
                    points[i] = resolved;
                }
            }
        }
        points[0] = root;
    }
};
} // namespace CharacterCreator
