#pragma once
#include "core/Draw.h"
#include "core/InputSource.h"
#include "core/Text.h"
#include <string>

namespace ui::glyph {

Source current();
Source pad();
bool has(const std::string& action, Source s = current());
float width(const std::string& action, float h, Source s = current());
float draw(Draw& d, const std::string& action, float x, float cy, float h, Color c = white(), Source s = current());
std::string name(const std::string& action, Source s = current());
std::string fill(const std::string& templ, Source s = current());
std::string swapLabel(Source s);
std::string swapNote(Source s);
float lineWidth(const std::string& templ, Font f, Source s = current());
float line(Draw& d, const std::string& templ, float x, float y, const TextStyle& st, Source s = current());

}
