#include "core/Text.h"
#include "platform/Platform.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <map>
#include <unordered_map>

namespace ui::text {

namespace {

struct Entry {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    uint64_t used = 0;
};

std::map<std::pair<int, int>, TTF_Font*> gFonts;
std::vector<TTF_Font*> gFallbacks;
std::unordered_map<std::string, Entry> gCache;
uint64_t gFrame = 0;
bool gBold = false;

Weight eff(Weight w) {
    if (!gBold) return w;
    return w == Weight::Bold ? Weight::Bold : Weight::Semibold;
}

TTF_Font* font(Font f) {
    Weight w = eff(f.weight);
    int sz = std::max(1, (int)std::lround(f.size * 4));
    auto key = std::make_pair((int)w, sz);
    auto it = gFonts.find(key);
    if (it != gFonts.end()) return it->second;
    TTF_Font* ft = nullptr;
    for (const auto& p : platform::fontPaths((int)w)) {
        ft = TTF_OpenFont(p.c_str(), sz / 4.f);
        if (ft) break;
    }
    if (ft) {
        TTF_SetFontHinting(ft, TTF_HINTING_LIGHT_SUBPIXEL);
        for (const auto& p : platform::fallbackFontPaths()) {
            TTF_Font* fb = TTF_OpenFont(p.c_str(), sz / 4.f);
            if (fb) {
                TTF_AddFallbackFont(ft, fb);
                gFallbacks.push_back(fb);
            }
        }
    } else {
        SDL_Log("no font for weight %d: %s", (int)w, SDL_GetError());
    }
    gFonts[key] = ft;
    return ft;
}

}

bool init() { return TTF_Init(); }

void shutdown() {
    for (auto& [k, e] : gCache) SDL_DestroyTexture(e.tex);
    gCache.clear();
    for (auto& [k, f] : gFonts) if (f) TTF_CloseFont(f);
    for (auto* f : gFallbacks) TTF_CloseFont(f);
    gFonts.clear();
    gFallbacks.clear();
    TTF_Quit();
}

void dropTextures() {
    for (auto& [k, e] : gCache) SDL_DestroyTexture(e.tex);
    gCache.clear();
}

void setBoldAll(bool on) { gBold = on; }

void endFrame() {
    gFrame++;
    if (gCache.size() < 600) return;
    for (auto it = gCache.begin(); it != gCache.end();) {
        if (gFrame - it->second.used > 120) {
            SDL_DestroyTexture(it->second.tex);
            it = gCache.erase(it);
        } else {
            ++it;
        }
    }
}

float width(const std::string& s, Font f) {
    TTF_Font* ft = font(f);
    if (!ft || s.empty()) return 0;
    int w = 0, h = 0;
    TTF_GetStringSize(ft, s.c_str(), s.size(), &w, &h);
    return (float)w;
}

float lineHeight(Font f) {
    TTF_Font* ft = font(f);
    return ft ? (float)TTF_GetFontHeight(ft) : f.size * 1.33f;
}

float ascent(Font f) {
    TTF_Font* ft = font(f);
    return ft ? (float)TTF_GetFontAscent(ft) : f.size;
}

std::string upper(const std::string& s) {
    std::string o = s;
    for (auto& c : o) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return o;
}

static size_t prevBoundary(const std::string& s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

std::string ellipsize(const std::string& s, Font f, float maxW) {
    if (width(s, f) <= maxW) return s;
    const std::string dots = "\xE2\x80\xA6";
    size_t n = s.size();
    while (n > 0) {
        n = prevBoundary(s, n);
        std::string t = s.substr(0, n);
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (width(t + dots, f) <= maxW) return t + dots;
    }
    return dots;
}

std::vector<std::string> wrap(const std::string& s, Font f, float maxW, int maxLines) {
    std::vector<std::string> lines;
    std::vector<std::string> words;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\n') {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
            if (c == '\n') words.push_back("\n");
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    std::string line;
    for (size_t i = 0; i < words.size(); i++) {
        const auto& w = words[i];
        if (w == "\n") {
            lines.push_back(line);
            line.clear();
            continue;
        }
        std::string trial = line.empty() ? w : line + " " + w;
        if (width(trial, f) <= maxW || line.empty()) {
            line = trial;
        } else {
            lines.push_back(line);
            line = w;
        }
        if (maxLines > 0 && (int)lines.size() == maxLines) break;
    }
    if (!line.empty() && (maxLines <= 0 || (int)lines.size() < maxLines)) lines.push_back(line);
    if (maxLines > 0 && (int)lines.size() >= maxLines) {
        std::string rest;
        size_t used = 0;
        for (int i = 0; i < maxLines - 1; i++) used += lines[i].size() + 1;
        rest = used < s.size() ? s.substr(used) : lines[maxLines - 1];
        lines.resize(maxLines);
        lines[maxLines - 1] = ellipsize(rest, f, maxW);
    }
    for (auto& l : lines) if (width(l, f) > maxW) l = ellipsize(l, f, maxW);
    return lines;
}

static Entry* get(Draw& d, const std::string& s, Font f) {
    TTF_Font* ft = font(f);
    if (!ft || s.empty()) return nullptr;
    std::string key = std::to_string((int)eff(f.weight)) + "|" + std::to_string(std::lround(f.size * 4)) + "|" + s;
    auto it = gCache.find(key);
    if (it != gCache.end()) {
        it->second.used = gFrame;
        return &it->second;
    }
    SDL_Surface* surf = TTF_RenderText_Blended(ft, s.c_str(), s.size(), SDL_Color{255, 255, 255, 255});
    if (!surf) return nullptr;
    Entry e;
    e.tex = SDL_CreateTextureFromSurface(d.sdl(), surf);
    e.w = surf->w;
    e.h = surf->h;
    e.used = gFrame;
    SDL_DestroySurface(surf);
    if (!e.tex) return nullptr;
    SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
    return &(gCache[key] = e);
}

float draw(Draw& d, const std::string& raw, float x, float y, const TextStyle& st) {
    std::string s = st.upper ? upper(raw) : raw;
    Font f = st.font();
    Entry* e = get(d, s, f);
    if (!e) return 0;
    float w = (float)e->w, h = (float)e->h;
    float px = x;
    if (st.align == Align::Center) px = x - w / 2;
    else if (st.align == Align::Right) px = x - w;
    float py = y;
    if (st.valign == VAlign::Middle) py = y - h / 2;
    else if (st.valign == VAlign::Baseline) py = y - ascent(f);
    px = std::round(px);
    py = std::round(py);
    float a = st.color.a * d.alphaMul();
    if (a <= 0) return w;
    if (st.shadow > 0) {
        SDL_SetTextureColorModFloat(e->tex, 0, 0, 0);
        float sh = st.shadow;
        const float offs[5][2] = {{0, sh * .12f}, {-sh * .25f, sh * .2f}, {sh * .25f, sh * .2f}, {0, sh * .4f}, {0, -sh * .1f}};
        SDL_SetTextureAlphaModFloat(e->tex, std::min(1.f, a * .22f));
        for (auto& o : offs) {
            Rect r{px + o[0], py + o[1], w, h};
            SDL_RenderTexture(d.sdl(), e->tex, nullptr, &r);
        }
    }
    SDL_SetTextureColorModFloat(e->tex, st.color.r, st.color.g, st.color.b);
    SDL_SetTextureAlphaModFloat(e->tex, std::min(1.f, a));
    Rect r{px, py, w, h};
    SDL_RenderTexture(d.sdl(), e->tex, nullptr, &r);
    return w;
}

float drawLines(Draw& d, const std::vector<std::string>& lines, float x, float y, float lineH, const TextStyle& st) {
    float lh = lineH > 0 ? lineH : lineHeight(st.font());
    float glyphH = lineHeight(st.font());
    for (size_t i = 0; i < lines.size(); i++) draw(d, lines[i], x, y + i * lh + (lh - glyphH) / 2, st);
    return lh * lines.size();
}

}
