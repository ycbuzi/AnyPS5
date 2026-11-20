#pragma once
#include "core/Images.h"
#include "core/Text.h"
#include "screens/settings.h"

namespace ui::sett {

struct ListGeom {
    Rect box;
    bool top = false;
};

void paintSettingsBg(Draw& c, float W, float H);
ListGeom listGeom(bool top);
void drawList(Draw& d, const Rows& R, const ListGeom& g, int sel, float& scroll, Rect& anchorOut,
    std::function<void(int, bool)> onRow = nullptr, std::function<void(float)> onScroll = nullptr);
void drawToggle(Draw& d, float right, float cy, bool on);
void drawBars(Draw& d, float x, float cy, int signal, Color c);
float spacedText(Draw& d, const std::string& s, float x, float y, const TextStyle& st, float spacing);
void selBox(Draw& d, Rect r, float radius);

}
