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
// New transition variants inspect the neighboring cell in the requested
// direction. Outer edges retain their original variants and air stays air.
template<class Occupied>
inline unsigned short selectAt(int x, int y, Occupied occupied) {
    const auto baseAt = [&](int column) {
        return select(occupied(column,y-1), occupied(column,y+1),
                      occupied(column-1,y), occupied(column+1,y));
    };
    const auto base = baseAt(x);
    if (base == 38) {
        if (occupied(x+1,y) && baseAt(x+1) == 40) return 100; // r
        if (occupied(x-1,y) && baseAt(x-1) == 39) return 103; // l
    }
    if (base == 10) {
        if (occupied(x-1,y) && baseAt(x-1) == 9) return 101;  // l
        if (occupied(x+1,y) && baseAt(x+1) == 11) return 102; // r
    }
    return base;
}
}
