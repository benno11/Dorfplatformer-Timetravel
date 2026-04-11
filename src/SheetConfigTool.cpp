#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

struct Frame {
    SDL_FRect rect{};
    std::string name;
    bool rotated = false;
};

static std::string basenameOnly(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

static void writePlist(const std::string& outPath, const std::string& imageName,
                       int texW, int texH, const std::vector<Frame>& frames) {
    std::ofstream out(outPath);
    if (!out) return;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out << "<plist version=\"1.0\">\n";
    out << "\t<dict>\n";
    out << "\t\t<key>frames</key>\n";
    out << "\t\t<dict>\n";

    for (const auto& f : frames) {
        int x = (int)std::round(f.rect.x);
        int y = (int)std::round(f.rect.y);
        int rectW = (int)std::round(f.rect.w);
        int rectH = (int)std::round(f.rect.h);
        int spriteW = rectW;
        int spriteH = rectH;
        if (f.rotated) std::swap(rectW, rectH);
        std::string name = f.name.empty() ? "frame" : f.name;
        out << "\t\t\t<key>" << name << ".png</key>\n";
        out << "\t\t\t<dict>\n";
        out << "\t\t\t\t<key>aliases</key>\n";
        out << "\t\t\t\t<array/>\n";
        out << "\t\t\t\t<key>spriteOffset</key>\n";
        out << "\t\t\t\t<string>{0,0}</string>\n";
        out << "\t\t\t\t<key>spriteSize</key>\n";
        out << "\t\t\t\t<string>{" << spriteW << "," << spriteH << "}</string>\n";
        out << "\t\t\t\t<key>spriteSourceSize</key>\n";
        out << "\t\t\t\t<string>{" << spriteW << "," << spriteH << "}</string>\n";
        out << "\t\t\t\t<key>textureRect</key>\n";
        out << "\t\t\t\t<string>{{" << x << "," << y << "},{" << rectW << "," << rectH << "}}</string>\n";
        out << "\t\t\t\t<key>textureRotated</key>\n";
        if (f.rotated) {
            out << "\t\t\t\t<true/>\n";
        } else {
            out << "\t\t\t\t<false/>\n";
        }
        out << "\t\t\t</dict>\n";
    }

    out << "\t\t</dict>\n";
    out << "\t\t<key>metadata</key>\n";
    out << "\t\t<dict>\n";
    out << "\t\t\t<key>format</key>\n";
    out << "\t\t\t<integer>3</integer>\n";
    out << "\t\t\t<key>pixelFormat</key>\n";
    out << "\t\t\t<string>RGBA8888</string>\n";
    out << "\t\t\t<key>premultiplyAlpha</key>\n";
    out << "\t\t\t<false/>\n";
    out << "\t\t\t<key>realTextureFileName</key>\n";
    out << "\t\t\t<string>" << imageName << "</string>\n";
    out << "\t\t\t<key>size</key>\n";
    out << "\t\t\t<string>{" << texW << "," << texH << "}</string>\n";
    out << "\t\t\t<key>textureFileName</key>\n";
    out << "\t\t\t<string>" << imageName << "</string>\n";
    out << "\t\t</dict>\n";
    out << "\t</dict>\n";
    out << "</plist>\n";
}

static std::string extractBetween(const std::string& s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    if (p == std::string::npos) return "";
    p += a.size();
    size_t q = s.find(b, p);
    if (q == std::string::npos) return "";
    return s.substr(p, q - p);
}

static bool parseTextureRect(const std::string& s, SDL_FRect& out) {
    // expects {{x,y},{w,h}}
    int x = 0, y = 0, w = 0, h = 0;
    if (std::sscanf(s.c_str(), "{{%d,%d},{%d,%d}}", &x, &y, &w, &h) == 4) {
        out.x = (float)x;
        out.y = (float)y;
        out.w = (float)w;
        out.h = (float)h;
        return true;
    }
    return false;
}

static void loadPlistFrames(const std::string& plistPath, std::vector<Frame>& frames) {
    std::ifstream in(plistPath);
    if (!in) return;

    std::string line;
    std::string currentName;
    bool expectTextureRect = false;
    bool expectRotated = false;
    Frame pending{};

    while (std::getline(in, line)) {
        std::string key = extractBetween(line, "<key>", "</key>");
        if (!key.empty() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            currentName = key.substr(0, key.size() - 4);
            pending = Frame{};
            pending.name = currentName.empty() ? "frame" : currentName;
        }
        if (line.find("<key>textureRect</key>") != std::string::npos) {
            expectTextureRect = true;
            continue;
        }
        if (line.find("<key>textureRotated</key>") != std::string::npos) {
            expectRotated = true;
            continue;
        }
        if (expectTextureRect) {
            std::string val = extractBetween(line, "<string>", "</string>");
            SDL_FRect r{};
            if (!val.empty() && parseTextureRect(val, r)) {
                pending.rect = r;
            }
            expectTextureRect = false;
        }
        if (expectRotated) {
            if (line.find("<true/>") != std::string::npos) pending.rotated = true;
            if (line.find("<false/>") != std::string::npos) pending.rotated = false;
            frames.push_back(pending);
            expectRotated = false;
        }
    }
}

static bool pointInRect(float px, float py, const SDL_FRect& r) {
    return px >= r.x && py >= r.y && px <= r.x + r.w && py <= r.y + r.h;
}

static SDL_FRect viewRect(const Frame& f) {
    if (!f.rotated) return f.rect;
    SDL_FRect r = f.rect;
    std::swap(r.w, r.h);
    return r;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        SDL_Log("Usage: sheet_config <image.png> <out.plist> [load.plist]");
        return 1;
    }

    std::string imagePath = argv[1];
    std::string outPath = argv[2];
    std::string loadPath = (argc >= 4) ? argv[3] : "";
    if (loadPath.empty()) {
        std::ifstream probe(outPath);
        if (probe.good()) loadPath = outPath;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Surface* surf = IMG_Load(imagePath.c_str());
    if (!surf) {
        SDL_Log("Failed to load image: %s", SDL_GetError());
        return 1;
    }

    int texW = surf->w;
    int texH = surf->h;
    int winW = std::min(1280, std::max(640, texW));
    int winH = std::min(720, std::max(480, texH));

    SDL_Window* win = SDL_CreateWindow("Sheet Config", winW, winH, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_FreeSurface(surf);

    bool running = true;
    bool dragging = false;
    bool panning = false;
    bool dragScrollH = false;
    bool dragScrollV = false;
    bool renaming = false;
    SDL_FPoint dragStart{0,0};
    SDL_FPoint panStart{0,0};
    SDL_FPoint scrollGrab{0,0};
    SDL_FPoint cam{0,0};
    SDL_FRect current{0,0,0,0};
    SDL_FRect hBar{0,0,0,0};
    SDL_FRect vBar{0,0,0,0};
    bool hasHBar = false;
    bool hasVBar = false;

    std::vector<Frame> frames;
    int selected = -1;
    std::string renameBuffer;

    if (!loadPath.empty()) {
        loadPlistFrames(loadPath, frames);
        if (!frames.empty()) {
            selected = 0;
            SDL_Log("Loaded %d frames from %s", (int)frames.size(), loadPath.c_str());
        } else {
            SDL_Log("No frames loaded from %s", loadPath.c_str());
        }
    }

    SDL_StartTextInput(win);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (renaming) continue;
                if (hasHBar && pointInRect((float)e.button.x, (float)e.button.y, hBar)) {
                    dragScrollH = true;
                    scrollGrab.x = (float)e.button.x - hBar.x;
                    continue;
                }
                if (hasVBar && pointInRect((float)e.button.x, (float)e.button.y, vBar)) {
                    dragScrollV = true;
                    scrollGrab.y = (float)e.button.y - vBar.y;
                    continue;
                }
                float mx = e.button.x + cam.x;
                float my = e.button.y + cam.y;
                selected = -1;
                for (int i = (int)frames.size() - 1; i >= 0; --i) {
                    if (pointInRect(mx, my, viewRect(frames[i]))) { selected = i; break; }
                }
                if (selected == -1) {
                    dragging = true;
                    dragStart = {mx, my};
                    current = {mx, my, 0, 0};
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                if (renaming) continue;
                panning = true;
                panStart = {(float)e.button.x, (float)e.button.y};
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (renaming) continue;
                if (dragging) {
                    dragging = false;
                    float x = std::min(dragStart.x, current.x + current.w);
                    float y = std::min(dragStart.y, current.y + current.h);
                    float w = std::fabs(current.w);
                    float h = std::fabs(current.h);
                    if (w >= 2 && h >= 2) {
                        Frame f;
                        f.rect = {x, y, w, h};
                        f.name = "frame_" + std::to_string((int)frames.size());
                        frames.push_back(f);
                        selected = (int)frames.size() - 1;
                    }
                }
                dragScrollH = false;
                dragScrollV = false;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                if (renaming) continue;
                panning = false;
            }
            if (e.type == SDL_MOUSEMOTION) {
                if (renaming) continue;
                if (dragScrollH && hasHBar) {
                    float trackW = (float)winW - hBar.w;
                    if (trackW > 0.0f) {
                        float px = (float)e.motion.x - scrollGrab.x;
                        px = std::clamp(px, 0.0f, trackW);
                        float t = px / trackW;
                        cam.x = t * (float)std::max(0, texW - winW);
                    }
                } else if (dragScrollV && hasVBar) {
                    float trackH = (float)winH - vBar.h;
                    if (trackH > 0.0f) {
                        float py = (float)e.motion.y - scrollGrab.y;
                        py = std::clamp(py, 0.0f, trackH);
                        float t = py / trackH;
                        cam.y = t * (float)std::max(0, texH - winH);
                    }
                }
                if (dragging) {
                    float mx = e.motion.x + cam.x;
                    float my = e.motion.y + cam.y;
                    current.w = mx - dragStart.x;
                    current.h = my - dragStart.y;
                }
                if (panning) {
                    cam.x -= (e.motion.x - panStart.x);
                    cam.y -= (e.motion.y - panStart.y);
                    cam.x = std::clamp(cam.x, 0.0f, (float)std::max(0, texW - winW));
                    cam.y = std::clamp(cam.y, 0.0f, (float)std::max(0, texH - winH));
                    panStart = {(float)e.motion.x, (float)e.motion.y};
                }
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.key == SDLK_ESCAPE) running = false;
                if (e.key.key == SDLK_DELETE && selected >= 0) {
                    frames.erase(frames.begin() + selected);
                    selected = -1;
                }
                if (!renaming && e.key.key == SDLK_t && selected >= 0) {
                    frames[selected].rotated = !frames[selected].rotated;
                }
                if (e.key.key == SDLK_s && (e.key.mod & KMOD_CTRL)) {
                    writePlist(outPath, basenameOnly(imagePath), texW, texH, frames);
                    SDL_Log("Saved %s", outPath.c_str());
                }
                if (renaming && e.key.key == SDLK_RETURN) {
                    frames[selected].name = renameBuffer;
                    renaming = false;
                }
                if (renaming && e.key.key == SDLK_BACKSPACE && !renameBuffer.empty()) {
                    renameBuffer.pop_back();
                }
                if (!renaming && e.key.key == SDLK_r && selected >= 0) {
                    renaming = true;
                    renameBuffer = frames[selected].name;
                }
            }
            if (e.type == SDL_TEXTINPUT && renaming) {
                renameBuffer += e.text.text;
            }
        }

        if (renaming && selected >= 0) {
            std::string title = "Rename: " + renameBuffer + " (Enter to confirm)";
            SDL_SetWindowTitle(win, title.c_str());
        } else if (selected >= 0) {
            std::string title = "Selected: " + frames[selected].name + " (R to rename, Ctrl+S to save)";
            SDL_SetWindowTitle(win, title.c_str());
        } else {
            SDL_SetWindowTitle(win, "Sheet Config (drag to add, right-drag to pan, Ctrl+S save)");
        }

        SDL_SetRenderDrawColor(ren, 18, 18, 22, 255);
        SDL_RenderClear(ren);

        SDL_FRect dst{-(float)(int)cam.x, -(float)(int)cam.y, (float)texW, (float)texH};
        SDL_RenderTexture(ren, tex, nullptr, &dst);

        SDL_SetRenderDrawColor(ren, 0, 200, 220, 255);
        for (int i = 0; i < (int)frames.size(); ++i) {
            SDL_FRect r = viewRect(frames[i]);
            r.x -= cam.x;
            r.y -= cam.y;
            if (i == selected) {
                SDL_SetRenderDrawColor(ren, 255, 210, 80, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 0, 200, 220, 255);
            }
            SDL_RenderDrawRectF(ren, &r);
        }

        if (dragging) {
            SDL_FRect r;
            r.x = std::min(dragStart.x, dragStart.x + current.w) - cam.x;
            r.y = std::min(dragStart.y, dragStart.y + current.h) - cam.y;
            r.w = std::fabs(current.w);
            r.h = std::fabs(current.h);
            SDL_SetRenderDrawColor(ren, 80, 160, 255, 255);
            SDL_RenderDrawRectF(ren, &r);
        }

        // Scrollbars
        hasHBar = texW > winW;
        hasVBar = texH > winH;
        const float barThickness = 12.0f;
        if (hasHBar) {
            float ratio = (float)winW / (float)texW;
            float barW = std::max(24.0f, ratio * winW);
            float trackW = (float)winW - barW;
            float t = (texW == winW) ? 0.0f : (cam.x / (float)(texW - winW));
            hBar = { t * trackW, (float)winH - barThickness, barW, barThickness };
            SDL_SetRenderDrawColor(ren, 40, 40, 52, 200);
            SDL_FRect track{0.0f, (float)winH - barThickness, (float)winW, barThickness};
            SDL_RenderFillRectF(ren, &track);
            SDL_SetRenderDrawColor(ren, 120, 130, 160, 220);
            SDL_RenderFillRectF(ren, &hBar);
        }
        if (hasVBar) {
            float ratio = (float)winH / (float)texH;
            float barH = std::max(24.0f, ratio * winH);
            float trackH = (float)winH - barH;
            float t = (texH == winH) ? 0.0f : (cam.y / (float)(texH - winH));
            vBar = { (float)winW - barThickness, t * trackH, barThickness, barH };
            SDL_SetRenderDrawColor(ren, 40, 40, 52, 200);
            SDL_FRect track{(float)winW - barThickness, 0.0f, barThickness, (float)winH};
            SDL_RenderFillRectF(ren, &track);
            SDL_SetRenderDrawColor(ren, 120, 130, 160, 220);
            SDL_RenderFillRectF(ren, &vBar);
        }

        SDL_RenderPresent(ren);
    }

    SDL_StopTextInput(win);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

