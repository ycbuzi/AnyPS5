#pragma once
#include "core/Anim.h"
#include "core/Draw.h"
#include <string>

namespace ui {

class Osd {
public:
    void show(const std::string& kind, int level, bool muted);
    void draw(Draw& d);

private:
    std::string kind_;
    int level_ = 0;
    bool muted_ = false;
    double until_ = 0;
    Tween in_;
};

}
