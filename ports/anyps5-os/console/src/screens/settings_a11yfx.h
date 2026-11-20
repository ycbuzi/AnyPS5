#pragma once
#include "core/App.h"

namespace ui::a11yfx {

bool active();
void begin(SDL_Renderer* r, int w, int h);
void end(SDL_Renderer* r);
bool action(const Action& a);
void focus(Rect r);
void colorMatrix(const std::string& kind, float strength, float m[9]);

}
