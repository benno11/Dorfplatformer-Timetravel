#pragma once
#include "TileMap.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace CyclingBlock {
inline constexpr int ticksPerSecond = 60;
inline constexpr int halfCycleTicks = 4 * ticksPerSecond;
inline constexpr int cycleTicks = 2 * halfCycleTicks;
struct State {
    int tick = 0;
    double remainder = 0;
};
inline bool overlap(const SDL_FRect& a, const SDL_FRect& b) {
    return a.x < b.x+b.w && a.x+a.w > b.x && a.y < b.y+b.h && a.y+a.h > b.y;
}
inline SDL_FRect nearPlayer(SDL_FRect rect, const Player& player, float wrapW, float wrapH) {
    if (wrapW > 0) rect.x -= std::round((rect.x+rect.w/2-player.x-player.w/2)/wrapW)*wrapW;
    if (wrapH > 0) rect.y -= std::round((rect.y+rect.h/2-player.y-player.h/2)/wrapH)*wrapH;
    return rect;
}
// All decisions use the same pre-tick tile snapshot. A blocked block waits for
// the shared direction to reverse; it never reverses on its own or enters tiles.
// setTile updates both the visible ID and the collision layers.
template<class SetTile, class PlayerBlocked>
bool step(TileMap& map, Player& player, int direction, SetTile setTile,
          PlayerBlocked playerBlocked, const std::vector<SDL_FRect>& blockers = {},
          float wrapW = 0, float wrapH = 0) {
    struct Move { int from, to; };
    std::vector<Move> moves;
    const float tile = static_cast<float>(map.tileSize);
    for (int y = 0; y < map.h; ++y) for (int x = 0; x < map.w; ++x) {
        const int from = y*map.w+x, nextY = y+direction;
        if (map.tileIds[from] != TileIds::CyclingBlock || nextY < 0 || nextY >= map.h) continue;
        const int to = nextY*map.w+x;
        // IDs 0, 1 and 2 are the level format's empty tiles. Decorative tiles,
        // water, one-way platforms and non-player-solid blocks still obstruct it.
        if (map.tileIds[to] > 2 || map.solid[to] || map.semisolid[to] || map.water[to]) continue;
        const SDL_FRect destination{x*tile, nextY*tile, tile, tile};
        if (std::any_of(blockers.begin(), blockers.end(), [&](const SDL_FRect& b){return overlap(destination,b);})) continue;
        moves.push_back({from,to});
    }
    float desiredY=player.y;
    bool contacted=false, carried=false;
    if (!player.freeMove) for (const auto& move : moves) {
        const auto old = nearPlayer({(move.from%map.w)*tile, (move.from/map.w)*tile, tile, tile}, player, wrapW, wrapH);
        const SDL_FRect body{player.x,player.y,static_cast<float>(player.w),static_cast<float>(player.h)};
        // PlayerController samples solid tiles through x + w - 1. A player
        // resting beside a wall can therefore overlap its visual edge by less
        // than one pixel. Use the same footprint before applying a vertical push.
        const bool sharesColumn = body.x + body.w - 1 >= old.x && body.x < old.x + old.w;
        if (!sharesColumn) continue;
        const bool rider = std::fabs(body.y+body.h-old.y) < 0.1f;
        const SDL_FRect swept{old.x,old.y+std::min(0,direction)*tile,tile,2*tile};
        if (!rider && !overlap(body,swept)) continue;
        const float pushedY = rider ? player.y+direction*tile :
            (direction < 0 ? old.y-tile-player.h : old.y+2*tile);
        desiredY=direction < 0 ? std::min(desiredY,pushedY) : std::max(desiredY,pushedY);
        contacted=true;carried=carried||rider;
    }
    for (const auto& move : moves) setTile(move.from, 2);
    // Resolve against stationary geometry along the entire push. The moving
    // group is absent during the sweep, then restored and checked at its final
    // positions. A rider spanning two blocks must only be carried once.
    bool crushed=false;
    if (contacted) {
        const float delta=desiredY-player.y;
        const int steps=std::max(1,static_cast<int>(std::ceil(std::fabs(delta))));
        for (int i=1; i<=steps; ++i) {
            const SDL_FRect next{player.x,player.y+delta*i/steps,static_cast<float>(player.w),static_cast<float>(player.h)};
            if (playerBlocked(next)) {crushed=true;break;}
        }
        if (!crushed) {
            player.y=desiredY;player.vy=0;player.onGround=carried||direction<0;
        }
    }
    for (const auto& move : moves) setTile(move.to, TileIds::CyclingBlock);
    if (contacted && !crushed) {
        const SDL_FRect body{player.x,player.y,static_cast<float>(player.w),static_cast<float>(player.h)};
        crushed=playerBlocked(body);
    }
    return crushed;
}
template<class SetTile, class PlayerBlocked>
bool advance(State& state, double seconds, TileMap& map, Player& player, SetTile setTile,
             PlayerBlocked playerBlocked, const std::vector<SDL_FRect>& blockers = {},
             float wrapW = 0, float wrapH = 0) {
    if (!std::isfinite(seconds) || seconds <= 0 || map.tileSize <= 0) return false;
    state.remainder += seconds*ticksPerSecond;
    while (state.remainder+1e-9 >= 1) {
        state.remainder=std::max(0.0,state.remainder-1);
        const int direction=state.tick < halfCycleTicks ? -1 : 1;
        state.tick=(state.tick+1)%cycleTicks;
        if (step(map,player,direction,setTile,playerBlocked,blockers,wrapW,wrapH)) return true;
    }
    return false;
}
}
