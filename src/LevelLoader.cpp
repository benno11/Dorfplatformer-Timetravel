#include "LevelLoader.h"

#include <SDL2/SDL.h>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

static constexpr int TILE_SIZE = 32;

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
        defs.push_back(c);
    }
    return defs;
}

static void applyBlockDefsToMaps(TileMap& map,
                                 const std::vector<unsigned short>& tileIds,
                                 const std::vector<char>& defs)
{
    const int total = (int)tileIds.size();

    if ((int)map.solid.size()     != total) map.solid.assign(total, 0);
    if ((int)map.semisolid.size() != total) map.semisolid.assign(total, 0);
    if ((int)map.water.size()     != total) map.water.assign(total, 0);

    for (int i = 0; i < total; i++) {
        unsigned short id = tileIds[i];
        char d = (id < defs.size()) ? defs[id] : 0;
        // Rules:
        // '=' => semisolid, '^' => water, '#' => air. Anything else => solid fallback.
        map.semisolid[i] = (d == '=') ? 1 : 0;
        map.water[i]     = (d == '^') ? 1 : 0;
        // Solid is "not air", excluding semisolid and water.
        map.solid[i]     = (d == '#' && d != '=' && d != '^') ? 1 : 0;
    }
}

static void normalizeHorizontalOffset(TileMap& map) {
    if (map.w <= 0 || map.h <= 0) return;
    int firstCol = -1;
    for (int x = 0; x < map.w; ++x) {
        bool any = false;
        for (int y = 0; y < map.h; ++y) {
            int idx = y * map.w + x;
            if (map.tileIds[idx] != 0 || map.bg[idx] != 0) { any = true; break; }
            if (map.solid[idx] || map.semisolid[idx] || map.water[idx]) { any = true; break; }
        }
        if (any) { firstCol = x; break; }
    }
    if (firstCol <= 0) return;

    for (int y = 0; y < map.h; ++y) {
        for (int x = 0; x < map.w; ++x) {
            int dst = y * map.w + x;
            int srcX = x + firstCol;
            if (srcX < map.w) {
                int src = y * map.w + srcX;
                map.tileIds[dst] = map.tileIds[src];
                map.bg[dst] = map.bg[src];
            } else {
                map.tileIds[dst] = 0;
                map.bg[dst] = 0;
            }
        }
    }
}

/* -----------------------------
   Legacy "chunk" parser
   Reads one token at a time:
   - numeric value (possibly missing)
   - remembers the last a-z letter seen in that token
   Tokens may look like: "12c" meaning value=12, runLetter='c'
   Or "" meaning empty
------------------------------ */
struct LegacyToken {
    bool hasValue = false;
    int value = 0;        // chunk.return
    char lastLetter = 0;  // chunk.lastletter (a-z)
    bool empty = true;    // chunk.return == ""
};

static bool isSep(char c) {
    // token separators: whitespace, underscores, commas, semicolons, newlines, etc.
    return std::isspace((unsigned char)c) || c == '_' || c == ',' || c == ';' || c == '|';
}

static bool nextLegacyToken(const std::string& s, size_t& i, LegacyToken& out) {
    // skip seps
    while (i < s.size() && isSep(s[i])) i++;
    if (i >= s.size()) return false;

    out = LegacyToken{};

    // Parse number part (optional)
    long v = 0;
    bool neg = false;
    bool anyDigit = false;
    if (s[i] == '-') { neg = true; i++; }
    while (i < s.size() && std::isdigit((unsigned char)s[i])) {
        anyDigit = true;
        v = v * 10 + (s[i] - '0');
        i++;
        if (v > 2'000'000'000L) break;
    }

    if (anyDigit) {
        out.hasValue = true;
        out.value = neg ? (int)(-v) : (int)v;
        out.empty = false;
    } else {
        out.hasValue = false;
        out.value = 0;
        out.empty = true;
    }

    // Parse single trailing letter (run-length) if present.
    if (i < s.size()) {
        char ch = s[i];
        if (ch >= 'a' && ch <= 'z') {
            out.lastLetter = ch;
            i++;
        } else if (ch >= 'A' && ch <= 'Z') {
            out.lastLetter = (char)std::tolower((unsigned char)ch);
            i++;
        }
    }

    // If we didn't consume digits or a letter, advance to avoid infinite loop.
    if (!anyDigit && out.lastLetter == 0) {
        i++;
    }

    return true;
}

/* -----------------------------
   Placement step: matches script

   temp.id starts at 1 in script. We'll use 0-based index.

   write
   temp.id += max_y
   if temp.id > total: temp.id += 1-total
   if temp.id > max_y: error
------------------------------ */
static bool placeTilesLikeScript(const std::string& fileData,
                                size_t& cursor,
                                int w, int h,
                                std::vector<unsigned short>& outRowMajorGrid,
                                int& outObjCountTokenIndex)
{
    const int total = w * h;

    // We'll build Tile_grid as column-major scratch Tileist first (1..total),
    // then convert to engine row-major (y*w+x) with y flipped.
    std::vector<unsigned short> tileList(total, 0);

    // Script: parse_next_chunk(); temp.id;
    // temp.id defaults to 0 in many systems; but in your earlier code you started at 1.
    // In Scratch-style LoadBlocks, start is 1.
    int tempId = 1; // 1-based

    LegacyToken t{};
    int tokenIndex = 0;

    // loopuntil(loader.id > Chunk.upperend):
    // We can't know upperend exactly; practical rule:
    // keep consuming tile tokens until we've "failed the step rule" OR until next token clearly is objectCount.
    //
    // Your script exits with e500 when wrap pushes temp.id > max_y.
    // We'll detect that and stop BEFORE objects, returning false if we stop too early.
    //
    // The simplest faithful approach:
    // continue placing until the stepping rule says "would error", then STOP reading tile tokens.
    while (true) {
        size_t savedCursor = cursor;

        if (!nextLegacyToken(fileData, cursor, t)) {
            // ran out
            outObjCountTokenIndex = tokenIndex;
            break;
        }
        tokenIndex++;

        // temp.block = chunk.return; if chunk.return == "" => temp.block = 2
        int block = 0;
        if (t.empty || !t.hasValue) {
            // matches "chunk.return == """: treat missing numeric as 2
            block = 2;
        } else {
            block = t.value;
        }

        // run length is based on chunk.lastletter in a-z
        // If no letter, treat as 'a' (1) to be tolerant.
        char rl = (t.lastLetter >= 'a' && t.lastLetter <= 'z') ? t.lastLetter : 'a';
        int run = (rl - 'a') + 1; // a=1 .. z=26

        for (int r = 0; r < run; r++) {
            // write TileList[tempId]
            int idx0 = tempId - 1;
            if (idx0 < 0 || idx0 >= total) {
                SDL_Log("Tile placement out of range (tempId=%d total=%d)", tempId, total);
                return false;
            }
            tileList[idx0] = (unsigned short)std::max(0, block);
            SDL_Log("Placing block %d at tempId %d (run %d/%d) tokenIndex=%d", block, tempId, r+1, run, tokenIndex);

            // temp.id += max_y
            tempId += h;

            // if temp.id > total: temp.id += 1-total
            if (tempId > total) {
                tempId += (1 - total);

                // if temp.id > max_y: return e500
                if (tempId > h) {
                    // This is the exact script stop condition; we consider tile section ended.
                    // Rewind cursor to before this token, so objectCount token can be read next.
                    cursor = savedCursor;
                    outObjCountTokenIndex = tokenIndex - 1;
                    goto TILE_DONE;
                }
            }
        }
    }

TILE_DONE:

    // Convert tileList (column-major) into engine row-major, with vertical flip.
    outRowMajorGrid.assign(total, 0);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int src = x * h + y;          // column-major
            int dst = (h - 1 - y) * w + x; // row-major, flipped vertically
            outRowMajorGrid[dst] = tileList[src];
        }
    }

    return true;
}

/* -----------------------------
   Object position mapping
------------------------------ */
static void posToXY(int pos, int h, float& x, float& y) {
    int xt = pos / h;
    int yt = pos % h;
    yt = (h - 1 - yt);
    x = xt * TILE_SIZE + TILE_SIZE / 2;
    y = yt * TILE_SIZE + TILE_SIZE / 2;
}

/* =========================================================
   MAIN: loads legacy tile_grid + objects
========================================================= */
bool loadLevelBNNLVL(const std::string& path,
                     TileMap& map,
                     std::vector<ObjectInstance>& objects,
                     LevelMeta& meta)
{
    std::string s = readWholeFile(path);
    if (s.empty()) return false;

    size_t cur = 0;
    LegacyToken t{};

    // parse_next_chunk: version
    if (!nextLegacyToken(s, cur, t) || !t.hasValue) return false;
    int ver = t.value;

    // parse_next_chunk: max_x
    if (!nextLegacyToken(s, cur, t) || !t.hasValue) return false;
    int w = t.value;

    // parse_next_chunk: max_y
    if (!nextLegacyToken(s, cur, t) || !t.hasValue) return false;
    int h = t.value;

    const float tileGridDivisor = 1;


    // Legacy files store tile_grid dimensions as (height, width).
    w /= tileGridDivisor;
    h /= tileGridDivisor;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;

    map.resize(w, h);

    const int total = w * h;
    if ((int)map.tileIds.size() != total) map.tileIds.assign(total, 0);
    if ((int)map.bg.size()      != total) map.bg.assign(total, 0);

    // parse_next_chunk: temp.id (optional; only consume if it's a pure number)
    // Some files omit this token; if the next token has a run-length letter,
    // it's the first tile token and should not be consumed here.
    {
        size_t savedCur = cur;
        if (!(nextLegacyToken(s, cur, t) && t.hasValue && t.lastLetter == 0)) {
            cur = savedCur;
        }
    }

    // Tile section
    std::vector<unsigned short> grid;
    int dummy = 0;
    if (!placeTilesLikeScript(s, cur, w, h, grid, dummy)) {
        SDL_Log("Legacy tile placement failed");
        return false;
    }

    map.tileIds = grid;

    auto defs = loadBlockDefs("assets/blockdefined.txt");
    applyBlockDefsToMaps(map, map.tileIds, defs);
    normalizeHorizontalOffset(map);
    applyBlockDefsToMaps(map, map.tileIds, defs);

    // Next chunk should be object count
    if (!nextLegacyToken(s, cur, t) || !t.hasValue) {
        // If missing, still OK: no objects
        objects.clear();
        meta.name = "Legacy " + std::to_string(ver);
        return true;
    }

    int objCount = t.value;
    if (objCount < 0) objCount = 0;
    if (objCount > 200000) objCount = 200000;

    objects.clear();
    for (int n = 0; n < objCount; n++) {
        LegacyToken posT{}, typeT{};
        if (!nextLegacyToken(s, cur, posT) || !posT.hasValue) break;
        if (!nextLegacyToken(s, cur, typeT) || !typeT.hasValue) break;

        ObjectInstance o;
        o.id = std::to_string(typeT.value); // or "legacy_"+...
        posToXY(posT.value, h, o.x, o.y);
        objects.push_back(o);
    }

    meta.name = "Legacy " + std::to_string(ver);
    SDL_Log("Legacy load OK: %s tiles=%d objects=%d", path.c_str(), total, (int)objects.size());
    return true;
}
