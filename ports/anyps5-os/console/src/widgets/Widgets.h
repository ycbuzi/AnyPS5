#pragma once
#include "core/Draw.h"
#include "core/Text.h"
#include <nlohmann/json.hpp>
#include <string>

namespace ui::widgets {

std::string initial(const nlohmann::json& profile);
void avatar(Draw& d, float cx, float cy, float diameter, const nlohmann::json& profile, float letterPx, Weight w = Weight::Semibold);
void clock(Draw& d, float right, float cy, float sizePx);
void pill(Draw& d, Rect r, const std::string& label, float textPx, bool selected);
void focusGlow(Draw& d, Rect r, float radius);

}
