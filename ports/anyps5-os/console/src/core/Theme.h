#pragma once
#include "core/Types.h"
#include <array>
#include <utility>

namespace ui {

struct Theme {
    int w = 1920;
    int h = 1080;
    float rem = 16.f;
    bool reduceMotion = false;
    bool boldText = false;
    bool highContrast = false;

    void resize(int width, int height) {
        w = width;
        h = height;
        rem = std::min(width / 120.f, height / 67.5f);
    }
    float r(float v) const { return v * rem; }
    float W() const { return (float)w; }
    float H() const { return (float)h; }
};

namespace col {
inline const Color fg = white(1.f);
inline const Color dim = white(.62f);
inline const Color faint = white(.38f);
inline const Color line = white(.14f);
inline const Color panel = rgba(18, 22, 38, .78f);
inline const Color ok = hex(0x5be3a5);
inline const Color warn = hex(0xffbe5c);
inline const Color bad = hex(0xff6b6b);
inline const Color page = hex(0x040814);
}

const std::array<std::pair<Color, Color>, 6>& avatarColors();

}
