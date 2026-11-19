#pragma once
namespace DefaultTiles {
inline bool contains(unsigned short id) {
    return (id >= 3 && id <= 11) || (id >= 38 && id <= 40) || (id >= 100 && id <= 103);
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
