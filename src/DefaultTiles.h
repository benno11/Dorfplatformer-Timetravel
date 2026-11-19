#pragma once
namespace DefaultTiles {
inline bool contains(unsigned short id) {
    return (id >= 3 && id <= 11) || (id >= 38 && id <= 40) || (id >= 100 && id <= 103);
}
inline unsigned short select(bool up, bool down, bool left, bool right) {
    if (!up && !down) {
        if (!left && right) return 103;
        if (left && !right) return 100;
        return 38;
    }
    if (!up) {
        if (!left) return 101;
        if (!right) return 102;
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
}
