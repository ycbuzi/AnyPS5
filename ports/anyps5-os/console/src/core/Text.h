#pragma once
#include "core/Draw.h"
#include <string>
#include <vector>

namespace ui {

enum class Weight { Light, Regular, Medium, Semibold, Bold };
enum class Align { Left, Center, Right };
enum class VAlign { Top, Middle, Baseline };

struct Font {
    float size = 16.f;
    Weight weight = Weight::Regular;
};

struct TextStyle {
    float size = 16.f;
    Weight weight = Weight::Regular;
    Color color = white();
    Align align = Align::Left;
    VAlign valign = VAlign::Top;
    float shadow = 0.f;
    bool upper = false;
    Font font() const { return {size, weight}; }
};

namespace text {

bool init();
void shutdown();
void dropTextures();
void setBoldAll(bool on);
void endFrame();
float width(const std::string& s, Font f);
float lineHeight(Font f);
float ascent(Font f);
std::string ellipsize(const std::string& s, Font f, float maxW);
std::vector<std::string> wrap(const std::string& s, Font f, float maxW, int maxLines = 0);
float draw(Draw& d, const std::string& s, float x, float y, const TextStyle& st);
float drawLines(Draw& d, const std::vector<std::string>& lines, float x, float y, float lineH, const TextStyle& st);
std::string upper(const std::string& s);

}

}
