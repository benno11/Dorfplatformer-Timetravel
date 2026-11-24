#pragma once
namespace DefaultTiles {
inline bool contains(unsigned short id) {
    return (id >= 3 && id <= 11) || (id >= 38 && id <= 40) || (id >= 100 && id <= 103);
}
// A slope joins only along filled corners/edges, not across its empty half.
// dx/dy locate the neighboring tile relative to the block being rendered.
inline bool joins(unsigned short id, int dx, int dy) {
    if (contains(id)) return true;
    unsigned corners = 0; // top-left, top-right, bottom-left, bottom-right
    switch (id) {
        case 83: corners = 1|4|8; break;
        case 84: corners = 4; break;
        case 85: // Slope infill blocks connect on every side and diagonal.
        case 88: corners = 1|2|4|8; break;
        case 86: corners = 2|4|8; break;
        case 87: corners = 8; break;
        default: return false;
    }
    const unsigned facing = dy < 0 ? (dx < 0 ? 8u : dx > 0 ? 4u : 12u)
        : dy > 0 ? (dx < 0 ? 2u : dx > 0 ? 1u : 3u)
        : dx < 0 ? 10u : 5u;
    return (corners & facing) == facing;
}
inline unsigned short select(bool up, bool down, bool left, bool right) {
    if (!up && !down) {
        if (!left && right) return 39;
        if (left && !right) return 40;
        return 38;
    }
    if (!up) {
        if (!left) return 9;
        if (!right) return 11;
        return 10;
    }
    if (!down) {
        if (!left) return 3;
        if (!right) return 5;
        return 4;
    }
    if (!left) return 6;
    if (!right) return 8;
    return 7;
}
// Grass extends into a wall which continues above the current cell. Inspect
// the adjacent grass cell, not the wall's own top/bottom neighbors.
template<class Occupied>
inline unsigned short selectAt(int x, int y, Occupied occupied) {
    const auto base = select(occupied(x,y-1), occupied(x,y+1),
                             occupied(x-1,y), occupied(x+1,y));
    if (!occupied(x,y-1)) return base; // Exposed grass keeps the old edge rules.
    const bool rightGrass = occupied(x+1,y) && !occupied(x+1,y-1);
    const bool leftGrass = occupied(x-1,y) && !occupied(x-1,y-1);
    if (rightGrass && !occupied(x+1,y+1)) return 100; // slim, grass to right
    if (leftGrass && !occupied(x-1,y+1)) return 103;  // slim, grass to left
    if (leftGrass) return 101;                     // normal, grass to left
    if (rightGrass) return 102;                    // normal, grass to right
    return base;
}
}
