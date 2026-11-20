#include "platform/Platform.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <sys/stat.h>

namespace platform {

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

static std::vector<std::string> existing(std::initializer_list<const char*> list) {
    std::vector<std::string> out;
    for (const char* p : list) if (fileExists(p)) out.emplace_back(p);
    return out;
}

static std::string fontDir() {
    const char* d = std::getenv("ANYPS5_FONT_DIR");
    return d ? d : "";
}

std::vector<std::string> fontPaths(int weight) {
#if defined(_WIN32)
    switch (weight) {
    case 0: return existing({"C:/Windows/Fonts/segoeuil.ttf", "C:/Windows/Fonts/segoeui.ttf"});
    case 2: return existing({"C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeui.ttf"});
    case 3: return existing({"C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf"});
    case 4: return existing({"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/seguisb.ttf"});
    default: return existing({"C:/Windows/Fonts/segoeui.ttf"});
    }
#elif defined(__APPLE__)
    switch (weight) {
    case 3:
    case 4: return existing({"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf"});
    default: return existing({"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"});
    }
#else
    const char* d1 = "/usr/share/fonts/noto/";
    const char* d2 = "/usr/share/fonts/truetype/noto/";
    const char* names[5][3] = {
        {"NotoSans-Light.ttf", "NotoSans-Regular.ttf", nullptr},
        {"NotoSans-Regular.ttf", nullptr, nullptr},
        {"NotoSans-Medium.ttf", "NotoSans-Regular.ttf", nullptr},
        {"NotoSans-SemiBold.ttf", "NotoSans-Medium.ttf", "NotoSans-Bold.ttf"},
        {"NotoSans-Bold.ttf", "NotoSans-SemiBold.ttf", nullptr},
    };
    std::vector<std::string> out;
    int w = weight < 0 || weight > 4 ? 1 : weight;
    for (const char* n : names[w]) {
        if (!n) continue;
        for (std::string d : {fontDir(), std::string(d1), std::string(d2)}) {
            if (d.empty()) continue;
            std::string p = d + n;
            if (fileExists(p)) out.push_back(p);
        }
    }
    for (auto& p : existing({"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"})) out.push_back(p);
    return out;
#endif
}

std::vector<std::string> fallbackFontPaths() {
#if defined(_WIN32)
    return existing({"C:/Windows/Fonts/seguisym.ttf", "C:/Windows/Fonts/seguiemj.ttf"});
#elif defined(__APPLE__)
    return existing({"/System/Library/Fonts/Supplemental/Arial Unicode.ttf"});
#else
    std::vector<std::string> own;
    for (const char* n : {"NotoSansSymbols2-Regular.ttf", "NotoSansSymbols-Regular.ttf"})
        if (!fontDir().empty() && fileExists(fontDir() + n)) own.push_back(fontDir() + n);
    if (!own.empty()) return own;
    return existing({"/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf", "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf",
                     "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"});
#endif
}

void preferVideoDriver() {
#if !defined(_WIN32) && !defined(__APPLE__)
    if (SDL_getenv("SDL_VIDEODRIVER")) return;
    if (!SDL_getenv("DISPLAY") && !SDL_getenv("WAYLAND_DISPLAY")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kmsdrm");
#endif
}

}
