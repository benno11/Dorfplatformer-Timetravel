#include "CharacterRenderer.h"
#include "CharacterAttributes.h"
#include "CharacterFile.h"
#include "CharacterSpriteImport.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    using namespace CharacterCreator;
    Appearance a;
    assert(!a.enabled);
    for (const char* part : {"enabled", "skin", "hair_color", "hair_style", "shirt", "pants", "hat", "accent"})
        assert(change(a, part));
    assert(a.enabled);
    const auto document = save(a);
    assert(document["experimental"] == true && document["version"] == 1);
    assert(save(load(nlohmann::json::parse(document.dump()))) == document);
    assert(!change(a, "unknown"));
    assert(save(a) == document);
    assert(change(a, "reset") && !a.enabled);
    assert(save(a) == save(Appearance{}));
    for (int i = 0; i < (int)skinColors.size(); ++i) change(a, "skin");
    assert(a.skin == 0);
    for (int i = 0; i < (int)hats.size(); ++i) change(a, "hat");
    assert(a.hat == 0);
    const auto malformed = load({{"skin", -200}, {"hair_style", "bad"}, {"hat", 999999},
        {"shirt", std::numeric_limits<uint64_t>::max()}, {"enabled", "true"}});
    assert(malformed.skin == 0 && malformed.hairStyle == 0 && malformed.hat == 3);
    assert(malformed.shirt == 9 && !malformed.enabled);
    assert(!load(nullptr).enabled && !load({{"version", 2}, {"enabled", true}}).enabled);
    for (const auto& preset : placeholderCharacters) {
        assert(change(a, std::string("preset:") + preset.id));
        assert(a.enabled && save(a) == save(preset.appearance));
        assert(save(load(save(a))) == save(a));
        const auto original = save(a);
        assert(change(a, "shirt") && save(a) != original);
    }
    const auto beforeUnknownPreset = save(a);
    assert(!change(a, "preset:missing") && save(a) == beforeUnknownPreset);
    a = Appearance{};
    a.enabled = true;
    a.speed = 4;
    a.acceleration = 0;
    a.jump = 3;
    a.gravity = 1;
    a.springiness = 4;
    a.build = 2;
    const MovementConfig base;
    const auto customMovement = movementFor(a, base, true);
    assert(std::fabs(customMovement.maxSpeedGround - base.maxSpeedGround * 1.25f) < 0.001f);
    assert(std::fabs(customMovement.accelGround - base.accelGround * 0.75f) < 0.001f);
    assert(std::fabs(customMovement.jumpSpeed - base.jumpSpeed * 1.1f) < 0.001f);
    assert(std::fabs(customMovement.gravityGround - base.gravityGround * 0.9f) < 0.001f);
    assert(movementFor(a, base, false).maxSpeedGround == base.maxSpeedGround);
    a.enabled = false;
    assert(movementFor(a, base, true).jumpSpeed == base.jumpSpeed);
    assert(save(load(save(a))) == save(a));
    assert(change(a, "reset_attributes") && a.build == 1 && a.speed == 2 && a.springiness == 2);
    assert(!change(a, "texture:missing"));
    for (const char* part : partIds) assert(change(a, std::string("texture:") + part));
    assert(save(load(save(a))) == save(a));
    const auto invalidTexture = load({{"textures", {6, -1, 100, "bad"}},
        {"texture_pixels", {"invalid", nullptr}}, {"speed", 9999}, {"gravity", -1}});
    assert(invalidTexture.textures[0] == 0 && invalidTexture.textures[1] == 0 &&
        invalidTexture.textures[2] == 0 && invalidTexture.speed == 4 && invalidTexture.gravity == 0);

    PartPhysics parts;
    parts.update(1.0f / 60, 0, 0, true, false, 1);
    parts.update(1.0f / 60, 700, 0, true, false, 1);
    for (int frame = 0; frame < 30; ++frame) parts.update(1.0f / 60, 700, 0, true, false, 1);
    assert(parts.headX.position < -0.1f && parts.hair.position < -0.5f);
    const auto frozen = parts;
    parts.update(0, -700, 1000, false, true, -1);
    assert(parts.hair.position == frozen.hair.position && parts.hatY.velocity == frozen.hatY.velocity);
    for (int frame = 0; frame < 360; ++frame) parts.update(1.0f / 60, 0, 0, true, false, 1);
    assert(std::fabs(parts.hair.position) < 0.001f && std::fabs(parts.arms.position) < 0.001f);
    PartPhysics landing;
    landing.update(1.0f / 60, 0, 1200, false, false, 1);
    landing.update(1.0f / 60, 0, 0, true, false, 1);
    assert(landing.headY.velocity > 0 && landing.hatY.velocity < 0);
    PartPhysics reference;
    for (int fps : {30, 60, 120, 240}) {
        PartPhysics simulation;
        simulation.update(1.0f / fps, 0, 0, true, false, 1);
        for (int frame = 1; frame <= fps * 2; ++frame) {
            const float speed = std::min(500.0f, frame * 1000.0f / fps);
            simulation.update(1.0f / fps, speed, 0, true, false, 1);
        }
        if (fps == 30) reference = simulation;
        assert(std::fabs(simulation.headX.position - reference.headX.position) < 0.04f);
        assert(std::fabs(simulation.hair.position - reference.hair.position) < 0.08f);
    }
    for (int frame = 0; frame < 2000; ++frame) {
        parts.update(frame % 4 ? 1.0f / 240 : 5.0f, frame % 2 ? 100000 : -100000,
            frame % 3 ? 2000 : -2000, frame % 7 == 0, frame % 5 == 0, frame % 2 ? 1 : -1);
        assert(std::isfinite(parts.hair.position) && std::fabs(parts.hair.position) <= 2.2f);
        assert(std::isfinite(parts.arms.velocity) && std::fabs(parts.arms.position) <= 0.85f);
        assert(std::isfinite(parts.hatY.position) && std::fabs(parts.hatY.position) <= 1.5f);
    }
    parts.update(std::numeric_limits<float>::quiet_NaN(), 0, 0, true, false, 1);
    assert(!parts.initialized && parts.hair.position == 0);

    assert(SDL_Init(0));
    SDL_Surface* tile = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA8888);
    assert(tile);
    assert(SDL_FillSurfaceRect(tile, nullptr, SDL_MapSurfaceRGBA(tile, 200, 50, 100, 128)));
    const std::string imported = textureFromSurface(tile);
    assert(validTexturePixels(imported));
    a.texturePixels[(int)Part::Hat] = imported;
    a.textures[(int)Part::Hat] = 6;
    const auto hatPixel = texturePixel(a, Part::Hat, colors[0], 0, 0);
    assert(hatPixel.r == 200 && hatPixel.g == 50 && hatPixel.b == 100 && hatPixel.a == 128);
    assert(a.textures[(int)Part::Shirt] == 1); // Importing a hat leaves the shirt alone.
    assert(save(load(save(a))) == save(a));
    assert(textureFromSurface(nullptr).empty());
    a.speed = 4;
    a.jump = 0;
    Appearance transferred;
    assert(importCharacter(exportCharacter(a), transferred) && save(transferred) == save(a));
    const auto beforeBadImport = save(transferred);
    for (const std::string& bad : {std::string("not json"), std::string("{}"),
        std::string(262145, 'x'), std::string("{\"format\":\"df-character\",\"version\":2}")}) {
        assert(!importCharacter(bad, transferred) && save(transferred) == beforeBadImport);
    }
    auto corrupted = nlohmann::json::parse(exportCharacter(a));
    corrupted["character"]["texture_pixels"][4] = "broken pixels";
    assert(!importCharacter(corrupted.dump(), transferred) && save(transferred) == beforeBadImport);
    const auto filePath = std::filesystem::temp_directory_path() /
        ("df-character-test-" + std::to_string(SDL_GetTicksNS()) + ".dfcharacter");
    assert(writeCharacterFile(filePath.u8string(), a));
    assert(readCharacterFile(filePath.u8string(), transferred) && save(transferred) == save(a));
    assert(std::filesystem::remove(filePath));
    assert(!readCharacterFile(filePath.u8string(), transferred) && save(transferred) == save(a));
    const auto packPath = filePath.string() + ".pack.json";
    const auto layerPath = filePath.string() + ".bmp";
    SDL_Surface* atlas = SDL_CreateSurface(176, 28, SDL_PIXELFORMAT_RGBA8888);
    assert(atlas);
    for (int frame = 0; frame < 11; ++frame) {
        const SDL_Rect rect{frame * 16, 0, 16, 28};
        assert(SDL_FillSurfaceRect(atlas, &rect, SDL_MapSurfaceRGBA(atlas, (Uint8)(frame * 20), 80, 120, 255)));
    }
    assert(SDL_SaveBMP(atlas, layerPath.c_str()));
    SDL_DestroySurface(atlas);
    std::vector<std::string> order;
    for (auto name : spriteFrameNames) order.push_back(name);
    std::swap(order[0], order[1]);
    const std::string layerName = std::filesystem::path(layerPath).filename().u8string();
    nlohmann::json pack{{"characters", {{"example", {{"frame_width", 16}, {"frame_height", 28},
        {"order", order}, {"hair_base", layerName}, {"bangs_overlay", layerName}}}}}};
    auto writePack = [&]() { std::ofstream out(packPath); out << pack.dump(); };
    writePack();
    std::vector<Appearance> packCharacters;
    const auto bitmapLoader = [](const char* path) { return SDL_LoadBMP(path); };
    assert(readSpritePack(packPath, packCharacters, bitmapLoader) && packCharacters.size() == 1);
    assert(packCharacters[0].sprites.valid() && packCharacters[0].enabled);
    // Non-canonical manifest ordering must map back to the correct animation frames.
    assert(rgbaPixel(packCharacters[0].sprites.base, 0).r == 20);
    assert(rgbaPixel(packCharacters[0].sprites.base, 16).r == 0);
    const auto beforeBadPack = save(packCharacters[0]);
    pack["characters"]["example"]["hair_base"] = "../outside.png";
    writePack();
    assert(!readSpritePack(packPath, packCharacters, bitmapLoader));
    assert(packCharacters.size() == 1 && save(packCharacters[0]) == beforeBadPack);
    pack["characters"]["example"]["hair_base"] = layerName;
    pack["characters"]["example"]["order"][1] = pack["characters"]["example"]["order"][0];
    writePack();
    assert(!readSpritePack(packPath, packCharacters, bitmapLoader));
    assert(std::filesystem::remove(packPath) && std::filesystem::remove(layerPath));
    Appearance spriteExample;
    if (argc > 2) {
        assert(readCharacterFile(argv[2], spriteExample) && spriteExample.sprites.valid());
        assert(importCharacter(exportCharacter(spriteExample), transferred));
        assert(save(spriteExample) == save(transferred));
    }
    SDL_DestroySurface(tile);
    SDL_Surface* surface = SDL_CreateSurface(640, 224, SDL_PIXELFORMAT_RGBA8888);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    SDL_SetRenderDrawColor(renderer, 17, 25, 38, 255);
    SDL_RenderClear(renderer);
    const Pose poses[]{Pose::Idle, Pose::Run, Pose::Jump, Pose::Slide, Pose::Fall, Pose::Hurt, Pose::Death};
    for (int i = 0; i < 16; ++i) {
        a.skin = i % skinColors.size();
        a.hairStyle = i % hairStyles.size();
        a.hairColor = i % colors.size();
        a.shirt = (i + 2) % colors.size();
        a.pants = i % colors.size();
        a.accent = (i + 4) % colors.size();
        a.hat = (i / 4) % hats.size();
        a.build = i % 3;
        for (int part = 0; part < 6; ++part) a.textures[part] = (i + part) % 6;
        if (i % 3 == 0) { a.textures[(int)Part::Hat] = 6; a.texturePixels[(int)Part::Hat] = imported; }
        if (argc > 2 && i < 8) a = spriteExample;
        else a.sprites = {};
        SDL_FRect dst{(float)((i % 8) * 80 + 8), (float)((i / 8) * 112 + 4), 64, 104};
        if (poses[i % 7] == Pose::Slide) { dst.y += 36; dst.h -= 36; }
        SDL_SetRenderDrawColor(renderer, 11, 22, 33, 44);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        PartPhysics motion;
        for (int frame = 0; frame < 30; ++frame)
            motion.update(1.0f / 60, i % 2 ? -500.0f : 500.0f, 0, true, poses[i % 7] == Pose::Slide, i % 2 ? -1 : 1);
        motion.update(1.0f / 60, 0, 1000, false, false, 1);
        motion.update(1.0f / 60, 0, 0, true, false, 1);
        render(renderer, a, dst, poses[i % 7], 0.12f, i % 2 ? -1 : 1, 220, &motion);
        Uint8 r, g, b, alpha;
        SDL_BlendMode blend;
        SDL_GetRenderDrawColor(renderer, &r, &g, &b, &alpha);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        assert(r == 11 && g == 22 && b == 33 && alpha == 44 && blend == SDL_BLENDMODE_NONE);
    }
    SDL_Surface* output = SDL_RenderReadPixels(renderer, nullptr);
    assert(output);
    int colored = 0;
    for (int y = 0; y < output->h; ++y) for (int x = 0; x < output->w; ++x) {
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(output, x, y, &r, &g, &b, &alpha));
        if (r != 17 || g != 25 || b != 38) {
            ++colored;
            // Every sprite must remain inside its allocated preview cell.
            assert(x % 80 >= 8 && x % 80 < 72 && y % 112 >= 4 && y % 112 < 108);
        }
    }
    assert(colored > 15000);
    if (argc > 1) assert(SDL_SaveBMP(output, argv[1]));
    SDL_DestroySurface(output);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
}
