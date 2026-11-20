#pragma once
#include "core/Anim.h"
#include "core/Draw.h"
#include "core/Input.h"
#include "core/Screen.h"
#include <functional>
#include <string>
#include <vector>

namespace ui::kit {

void setbg(Draw& d);
void cup(Draw& d, const std::string& grade, float cx, float cy, float size);
void glow(Draw& d, Rect r, float radius);
SDL_Texture* dimGray(Draw& d, const std::string& url, float brightness);
float scrollNearest(float scroll, float top, float bottom, float viewH, float contentH);

struct MenuItem {
    std::string label;
    std::function<void()> run;
    bool sep = false;
};

class PsMenu {
public:
    bool open() const { return open_; }
    void show(std::vector<MenuItem> items, Rect anchor);
    void close() { open_ = false; }
    bool action(const Action& a);
    void draw(Draw& d);
    static std::vector<Hint> legend() { return {{"Select", "ok"}, {"Back", "back"}}; }

private:
    std::vector<MenuItem> items_;
    Rect anchor_{};
    int sel_ = 0;
    bool open_ = false;
    Tween in_;
};

}
