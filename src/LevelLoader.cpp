// src/LevelLoader.cpp
#include "LevelLoader.h"

#include <SDL2/SDL.h> // SDL_Log
#include <fstream>
#include <string>
#include <vector>
#include <cctype>

static constexpr int DEFAULT_TILE_SIZE = 32;

/* -----------------------------
   File helpers
------------------------------ */
static std::string readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::vector<char> loadBlockDefs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::vector<char> defs;
    std::string line;
    while (std::getline(f, line)) {
        char c = 0;
        for (char ch : line) {
            if (!std::isspace((unsigned char)ch)) { c = ch; break; }
        }
        defs.push_back(c); // 0 = empty/unmapped
    }
    return defs;
}

/* -----------------------------
   Tile defs -> collision masks
------------------------------ */
static void applyBlockDefsToMaps(
    TileMap& map,
    const std::vector<unsigned short>& tileIds,
    const std::vector<char>& defs
) {
    const int total = (int)tileIds.size();

    if ((int)map.solid.size()     != total) map.solid.assign(total, 0);
    if ((int)map.semisolid.size() != total) map.semisolid.assign(total, 0);
    if ((int)map.water.size()     != total) map.water.assign(total, 0);

    if (defs.empty()) {
        int solidCount = 0;
        for (int i = 0; i < total; i++) {
            map.solid[i] = tileIds[i] ? 1 : 0;
            map.semisolid[i] = 0;
            map.water[i] = 0;
            if (map.solid[i]) solidCount++;
        }
        SDL_Log("Tile assign (no defs): solid=%d empty=%d", solidCount, total - solidCount);
        return;
    }

    int solidCount = 0, semiCount = 0, waterCount = 0;
    for (int i = 0; i < total; i++) {
        unsigned short id = tileIds[i];
        char d = (id < (int)defs.size()) ? defs[id] : 0;

        map.solid[i]     = (d == '#') ? 1 : 0;
        map.semisolid[i] = (d == '=') ? 1 : 0;
        map.water[i]     = (d == '^') ? 1 : 0;

        if (map.solid[i]) solidCount++;
        if (map.semisolid[i]) semiCount++;
        if (map.water[i]) waterCount++;
    }

    SDL_Log("Tile assign: solid=%d semisolid=%d water=%d empty=%d",
            solidCount, semiCount, waterCount,
            total - solidCount - semiCount - waterCount);
}

/* -----------------------------
   Debug dump
------------------------------ */
static void dumpTileGridFlat(const std::string& filename, const std::vector<unsigned short>& tileIds) {
    std::ofstream out(filename);
    if (!out) return;
    for (size_t i = 0; i < tileIds.size(); i++) {
        if (i) out << ",";
        out << tileIds[i];
    }
    out << "\n";
}

/* -----------------------------
   Legacy parsing
------------------------------ */
/*
OLD FORMAT (one line per level file):
  <ver>_<w>_<h>_<tileRLE>_<pos>_<type>_<pos>_<type>_...

tileRLE contains a-z and digits.
We treat:
 - letter + digits => run
 - bare letter     => count=1
Mapping:
  z -> 0 (air)
  a..y -> 1..25 (tile IDs)
*/

// Parse integer, consuming optional underscore OR end-of-string.
static bool parseUIntUntilUnderscoreOrEnd(const std::string& s, size_t& i, int& out) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return false;

    long v = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) {
        v = v * 10 + (s[i] - '0');
        i++;
        if (v > 1'000'000'000) return false;
    }

    if (i < s.size() && s[i] == '_') i++;
    out = (int)v;
    return true;
}

/*
Expand the legacy RLE stream into a linear sequence of tile IDs.
IMPORTANT: Do NOT break when a letter has no digits; treat as count=1.
*/
static std::vector<unsigned short> decodeOldTileRLE_Stream(const std::string& rle, int total) {
    std::vector<unsigned short> out;
    out.reserve(total);

    size_t i = 0;
    while (i < rle.size() && (int)out.size() < total) {
        char ch = rle[i];

        // Skip junk safely
        if (!(ch >= 'a' && ch <= 'z')) { i++; continue; }
        i++;

        int cnt = 0;
        bool hasDigits = false;
        while (i < rle.size() && std::isdigit((unsigned char)rle[i])) {
            hasDigits = true;
            cnt = cnt * 10 + (rle[i] - '0');
            i++;
        }
        if (!hasDigits) cnt = 1;

        unsigned short val = (ch == 'z') ? 0 : (unsigned short)(ch - 'a' + 1);

        for (int k = 0; k < cnt && (int)out.size() < total; k++)
            out.push_back(val);
    }

    if ((int)out.size() < total) out.resize(total, 0);
    return out;
}

/*
This is the KEY: reproduce Scratch LoadBlocks placement.

Scratch pseudo (1-based):
  temp01 = 1
  for each tile in stream:
      Tileist[temp01] = tile
      temp01 += h
      if temp01 > total: temp01 += 1 - total
      if temp01 > h: stop

We implement it 0-based:
  idx = 0
  idx += h
  if idx >= total: idx += 1 - total
  if idx >= h: stop
*/
static std::vector<unsigned short> placeStreamLikeLoadBlocks(
    const std::vector<unsigned short>& stream,
    int w, int h
) {
    const int total = w * h;
    std::vector<unsigned short> tileist(total, 0);

    int idx = 0; // temp01-1
    int written = 0;

    for (int si = 0; si < (int)stream.size(); si++) {
        tileist[idx] = stream[si];
        written++;

        idx += h;
        if (idx >= total) idx += 1 - total;
        if (idx >= h) break; // matches Scratch "if temp01 > LevelHeight then stop"
    }

    SDL_Log("LoadBlocks placement: wrote=%d/%d", written, total);
    return tileist;
}

/*
Convert column-major Tileist (index = x*h + y) into your engine row-major grid.
Also flips Y (bottom-up -> top-down) to match your earlier tile drawing conventions.
*/
static std::vector<unsigned short> tileistToRowMajorFlipped(
    const std::vector<unsigned short>& tileist,
    int w, int h
) {
    const int total = w * h;
    std::vector<unsigned short> out(total, 0);

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int src = x * h + y;            // column-major
            int yFlip = (h - 1 - y);        // bottom-up -> top-down
            int dst = yFlip * w + x;        // row-major
            out[dst] = tileist[src];
        }
    }
    return out;
}

// Object position -> world pixels, matching the SAME Y-flip used for tile insertion.
static void posToXYLegacy(int pos, int h, int tileSize, float& x, float& y) {
    int xTile = pos / h;
    int yTile = pos % h;

    int yTileFlipped = (h - 1 - yTile);

    x = xTile * (float)tileSize + (float)(tileSize / 2);
    y = yTileFlipped * (float)tileSize + (float)(tileSize / 2);
}

static bool loadLevelOLDLINE(const std::string& line,
                             TileMap& map,
                             std::vector<ObjectInstance>& objects,
                             LevelMeta& meta)
{
    size_t i = 0;

    int ver = 0, w = 0, h = 0;
    if (!parseUIntUntilUnderscoreOrEnd(line, i, ver)) return false;
    if (!parseUIntUntilUnderscoreOrEnd(line, i, w)) return false;
    if (!parseUIntUntilUnderscoreOrEnd(line, i, h)) return false;
    if (w <= 0 || h <= 0) return false;

    // tileRLE goes until next '_' (it contains no underscores)
    size_t nextUS = line.find('_', i);
    if (nextUS == std::string::npos) {
        SDL_Log("Legacy parse: missing '_' after tileRLE");
        return false;
    }

    std::string tileRle = line.substr(i, nextUS - i);
    i = nextUS + 1;

    map.resize(w, h);

    const int total = w * h;
    if ((int)map.bg.size() != total) map.bg.assign(total, 0);

    // 1) Expand RLE into a linear stream
    std::vector<unsigned short> stream = decodeOldTileRLE_Stream(tileRle, total);

    // 2) Place into Tileist using Scratch LoadBlocks stepping
    std::vector<unsigned short> tileist = placeStreamLikeLoadBlocks(stream, w, h);

    // 3) Convert to engine row-major + flipped Y tile grid
    std::vector<unsigned short> tileGrid = tileistToRowMajorFlipped(tileist, w, h);

    // Keep tile ids on map (for rendering/editor/save)
    if ((int)map.tileIds.size() != total) map.tileIds.assign(total, 0);
    map.tileIds = tileGrid;

    dumpTileGridFlat("level-temp.data", tileGrid);

    auto defs = loadBlockDefs("assets/blockdefined.txt");
    applyBlockDefsToMaps(map, tileGrid, defs);

    // Objects: pairs <pos>_<type>_... (underscore at end is optional)
    objects.clear();
    while (i < line.size()) {
        int pos = 0, typ = 0;
        if (!parseUIntUntilUnderscoreOrEnd(line, i, pos)) break;
        if (!parseUIntUntilUnderscoreOrEnd(line, i, typ)) break;

        ObjectInstance inst;
        inst.id = "legacy_" + std::to_string(typ);
        posToXYLegacy(pos, h, DEFAULT_TILE_SIZE, inst.x, inst.y);
        objects.push_back(inst);
    }

    meta.name = "Legacy " + std::to_string(ver) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
    SDL_Log("Legacy loaded: tiles=%d objects=%d", total, (int)objects.size());
    return true;
}

/* -----------------------------
   Public entry
------------------------------ */
bool loadLevelBNNLVL(const std::string& path,
                     TileMap& map,
                     std::vector<ObjectInstance>& objects,
                     LevelMeta& meta)
{
    SDL_Log("Loading legacy level: %s", path.c_str());

    std::string contents = readWholeFile(path);
    if (contents.empty()) {
        SDL_Log("Failed to read level file: %s", path.c_str());
        return false;
    }

    // Old format: each file contains one line. Trim leading whitespace/newlines.
    std::string line;
    {
        size_t a = 0;
        while (a < contents.size() && std::isspace((unsigned char)contents[a])) a++;
        size_t b = a;
        while (b < contents.size() && contents[b] != '\n' && contents[b] != '\r') b++;
        line = contents.substr(a, b - a);
    }

    bool ok = loadLevelOLDLINE(line, map, objects, meta);
    SDL_Log(ok ? "Legacy level loaded" : "Legacy level load failed");
    return ok;
}
