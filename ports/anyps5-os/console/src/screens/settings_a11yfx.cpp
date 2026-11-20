#include "screens/settings_a11yfx.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace ui::a11yfx {

namespace {

struct Fx {
    SDL_Texture* target = nullptr;
    SDL_Texture* stream = nullptr;
    int w = 0, h = 0;
    unsigned gen = 0;
    bool began = false, zoomOn = false, haveFocus = false;
    Rect focus{0, 0, 0, 0};
};

Fx F;

std::string str(const char* k, const char* d) {
    json v = app().settings.a11yv(k, d);
    return v.is_string() ? v.get<std::string>() : d;
}

bool zoomActive() {
    if (!app().settings.a11y("zoom")) F.zoomOn = false;
    return F.zoomOn;
}

bool colorActive() { return str("cc", "off") != "off" || app().settings.a11y("invert"); }

const float* lin() {
    static float t[256];
    static bool done = false;
    if (!done) {
        for (int i = 0; i < 256; i++) {
            float c = i / 255.f;
            t[i] = c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
        }
        done = true;
    }
    return t;
}

const uint8_t* srgb() {
    static uint8_t t[4096];
    static bool done = false;
    if (!done) {
        for (int i = 0; i < 4096; i++) {
            float l = i / 4095.f;
            float c = l <= .0031308f ? l * 12.92f : 1.055f * std::pow(l, 1 / 2.4f) - .055f;
            t[i] = (uint8_t)std::lround(std::clamp(c, 0.f, 1.f) * 255);
        }
        done = true;
    }
    return t;
}

struct Pipe {
    bool dalton = false, gray = false, invert = false;
    float m[9];
};

uint8_t to8(float v) { return (uint8_t)std::lround(std::clamp(v, 0.f, 1.f) * 255); }

void run(const Pipe& p, uint32_t* px, int pitch, int y0, int y1, int w) {
    const float* L = lin();
    const uint8_t* S = srgb();
    static const float H[9] = {-.574f, 1.43f, .144f, .426f, .43f, .144f, .426f, 1.43f, -.856f};
    for (int y = y0; y < y1; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)px + (size_t)y * pitch);
        for (int x = 0; x < w; x++) {
            uint32_t v = row[x];
            int R = (v >> 16) & 255, G = (v >> 8) & 255, B = v & 255;
            float r, g, b;
            if (p.dalton) {
                float lr = L[R], lg = L[G], lb = L[B];
                float nr = p.m[0] * lr + p.m[1] * lg + p.m[2] * lb;
                float ng = p.m[3] * lr + p.m[4] * lg + p.m[5] * lb;
                float nb = p.m[6] * lr + p.m[7] * lg + p.m[8] * lb;
                r = S[(int)(std::clamp(nr, 0.f, 1.f) * 4095)] / 255.f;
                g = S[(int)(std::clamp(ng, 0.f, 1.f) * 4095)] / 255.f;
                b = S[(int)(std::clamp(nb, 0.f, 1.f) * 4095)] / 255.f;
            } else {
                r = R / 255.f;
                g = G / 255.f;
                b = B / 255.f;
            }
            if (p.gray) r = g = b = .2126f * r + .7152f * g + .0722f * b;
            if (p.invert) {
                r = 1 - r;
                g = 1 - g;
                b = 1 - b;
                float nr = H[0] * r + H[1] * g + H[2] * b, ng = H[3] * r + H[4] * g + H[5] * b, nb = H[6] * r + H[7] * g + H[8] * b;
                r = nr;
                g = ng;
                b = nb;
            }
            row[x] = (v & 0xff000000u) | ((uint32_t)to8(r) << 16) | ((uint32_t)to8(g) << 8) | to8(b);
        }
    }
}

void ensure(SDL_Renderer* r, int w, int h) {
    if (F.gen != Draw::generation()) F.target = F.stream = nullptr;
    F.gen = Draw::generation();
    if (F.target && F.w == w && F.h == h) return;
    if (F.target) SDL_DestroyTexture(F.target);
    if (F.stream) SDL_DestroyTexture(F.stream);
    F.target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    F.stream = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (F.target) SDL_SetTextureBlendMode(F.target, SDL_BLENDMODE_NONE);
    if (F.stream) SDL_SetTextureBlendMode(F.stream, SDL_BLENDMODE_NONE);
    F.w = w;
    F.h = h;
}

SDL_Texture* recolor(SDL_Renderer* r) {
    SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
    if (!s) return F.target;
    SDL_Surface* c = s->format == SDL_PIXELFORMAT_ARGB8888 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    if (!c) {
        SDL_DestroySurface(s);
        return F.target;
    }
    Pipe p;
    std::string cc = str("cc", "off");
    p.gray = cc == "gray";
    p.dalton = cc == "protan" || cc == "deutan" || cc == "tritan";
    p.invert = app().settings.a11y("invert");
    if (p.dalton) {
        json k = app().settings.a11yv("ccStrength", 1);
        colorMatrix(cc, k.is_number() ? k.get<float>() : 1.f, p.m);
    }
    int n = std::clamp((int)std::thread::hardware_concurrency(), 1, 4);
    std::vector<std::thread> pool;
    int step = (c->h + n - 1) / n;
    for (int i = 0; i < n; i++) {
        int y0 = i * step, y1 = std::min(c->h, y0 + step);
        if (y0 < y1) pool.emplace_back(run, std::cref(p), (uint32_t*)c->pixels, c->pitch, y0, y1, c->w);
    }
    for (auto& t : pool) t.join();
    SDL_UpdateTexture(F.stream, nullptr, c->pixels, c->pitch);
    if (c != s) SDL_DestroySurface(c);
    SDL_DestroySurface(s);
    return F.stream;
}

}

void colorMatrix(const std::string& kind, float k, float m[9]) {
    static const float protan[9] = {.152286f, 1.052583f, -.204868f, .114503f, .786281f, .099216f, -.003882f, -.048116f, 1.051998f};
    static const float deutan[9] = {.367322f, .860646f, -.227968f, .280085f, .672501f, .047413f, -.01182f, .04294f, .968881f};
    static const float tritan[9] = {1.255528f, -.076749f, -.178779f, -.078411f, .930809f, .147602f, .004733f, .691367f, .3039f};
    static const float eT[9] = {1, 0, .7f, 0, 1, .7f, 0, 0, 0};
    static const float eRG[9] = {0, 0, 0, .7f, 1, 0, .7f, 0, 1};
    const float* s = kind == "protan" ? protan : kind == "deutan" ? deutan : tritan;
    const float* e = kind == "tritan" ? eT : eRG;
    float d[9];
    for (int i = 0; i < 9; i++) d[i] = (i % 4 == 0 ? 1.f : 0.f) - s[i];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float ed = e[i * 3] * d[j] + e[i * 3 + 1] * d[3 + j] + e[i * 3 + 2] * d[6 + j];
            m[i * 3 + j] = (i == j ? 1.f : 0.f) + k * ed;
        }
}

bool active() { return zoomActive() || colorActive(); }

void focus(Rect r) {
    F.focus = r;
    F.haveFocus = true;
}

void begin(SDL_Renderer* r, int w, int h) {
    F.began = active();
    F.haveFocus = false;
    if (!F.began) return;
    ensure(r, w, h);
    if (!F.target || !F.stream) {
        F.began = false;
        return;
    }
    SDL_SetRenderTarget(r, F.target);
}

void end(SDL_Renderer* r) {
    if (!F.began) return;
    F.began = false;
    SDL_Texture* src = colorActive() ? recolor(r) : F.target;
    SDL_SetRenderTarget(r, nullptr);
    SDL_FRect sr{0, 0, (float)F.w, (float)F.h};
    if (zoomActive()) {
        float cx = F.haveFocus ? F.focus.x + F.focus.w / 2 : F.w / 2.f;
        float cy = F.haveFocus ? F.focus.y + F.focus.h / 2 : F.h / 2.f;
        sr = {cx / 2, cy / 2, F.w / 2.f, F.h / 2.f};
    }
    SDL_RenderTexture(r, src, &sr, nullptr);
}

bool action(const Action& a) {
    if (!a.is("zoom")) return false;
    if (!app().settings.a11y("zoom")) return false;
    F.zoomOn = !F.zoomOn;
    return true;
}

}
