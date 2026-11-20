#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <algorithm>

namespace ui {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

inline Color rgba(int r, int g, int b, float a = 1.f) { return {r / 255.f, g / 255.f, b / 255.f, a}; }
inline Color hex(uint32_t v, float a = 1.f) { return rgba((v >> 16) & 255, (v >> 8) & 255, v & 255, a); }
inline Color white(float a = 1.f) { return {1, 1, 1, a}; }
inline Color black(float a = 1.f) { return {0, 0, 0, a}; }
inline Color alpha(Color c, float a) { c.a *= a; return c; }
inline Color mix(Color x, Color y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

using Rect = SDL_FRect;

inline Rect inset(Rect r, float d) { return {r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d}; }
inline Rect offset(Rect r, float dx, float dy) { return {r.x + dx, r.y + dy, r.w, r.h}; }
inline bool contains(Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

struct Stop {
    float pos;
    Color color;
};

}
