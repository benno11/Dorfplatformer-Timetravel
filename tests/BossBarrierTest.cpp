#include "PlayerController.h"
#include "TileMap.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    TileMap map;
    map.resize(4, 4);
    const int cell = 2 * map.w + 2;
    map.tileIds[cell] = TileIds::BossBarrier;
    map.setBlockDefinition(cell, '#');
    require(!map.solid[cell] && !map.semisolid[cell] && !map.water[cell],
        "Reserved boss barrier must remain air for players");
    require(!RectHitsSolid(map, 64, 64, 30, 30), "Players must pass through a boss barrier");
    require(RectHitsBossSolid(map, 64, 64, 30, 30), "Bosses must hit a boss barrier");
    require(!RectHitsBossSolid(map, 34, 64, 30, 30), "Touching the edge must not count as overlap");
    require(RectHitsBossSolid(map, 35, 64, 30, 30), "One pixel of overlap must block a boss");

    map.tileIds[cell] = 7;
    map.setBlockDefinition(cell, '#');
    require(RectHitsSolid(map, 64, 64, 30, 30) && RectHitsBossSolid(map, 64, 64, 30, 30),
        "Ordinary walls must still block both actors");
    map.tileIds[cell] = 2;
    map.setBlockDefinition(cell, 0);
    require(!RectHitsBossSolid(map, 64, 64, 30, 30), "Removing a barrier must clear boss collision");

    map.tileIds[0] = TileIds::BossBarrier;
    map.setBlockDefinition(0, '^');
    require(!map.water[0], "Boss barriers must not affect player water physics");
    SetHorizontalWrapCollision(true);
    require(RectHitsBossSolid(map, 128, 0, 30, 30), "Boss barriers must work across horizontal wrapping");
    require(!RectHitsSolid(map, 128, 0, 30, 30), "Wrapped barriers must remain passable to players");
    SetVerticalWrapCollision(true);
    require(RectHitsBossSolid(map, 0, 128, 30, 30), "Boss barriers must work across vertical wrapping");
    require(!RectHitsSolid(map, 0, 128, 30, 30), "Vertical wrapped barriers must remain player air");
    SetHorizontalWrapCollision(false);
    SetVerticalWrapCollision(false);
    std::cout << "Boss barrier collision tests passed\n";
}
