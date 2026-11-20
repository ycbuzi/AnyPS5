#pragma once
#include "core/Anim.h"
#include "core/Draw.h"
#include <string>
#include <vector>

namespace ui {

struct Note {
    std::string text;
    std::string src;
    std::string icon;
    double at = 0;
    long long wall = 0;
};

class Toasts {
public:
    void push(const std::string& text, const std::string& src = "AnyPS5", const std::string& icon = "");
    void draw(Draw& d);
    void update();
    std::vector<Note>& history() { return history_; }
    bool popups = true;
    double seconds = 3;

private:
    struct Live {
        Note n;
        Tween in;
    };
    std::vector<Live> live_;
    std::vector<Note> history_;
};

}
