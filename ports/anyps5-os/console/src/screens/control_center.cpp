#include "screens/control_center_screen.h"
#include <algorithm>

namespace ui::cc {

float iconX(int i) {
    const Theme& t = app().theme;
    float total = kIcons * t.r(2.2f) + (kIcons - 1) * t.r(4.9f);
    return t.W() / 2 - total / 2 + t.r(1.1f) + i * t.r(7.1f);
}

float iconY() {
    const Theme& t = app().theme;
    return t.H() - t.r(3.4f) - t.r(1.1f);
}

void ControlCenter::enter() {
    crow_ = 0;
    card_ = 0;
    qsel_ = 0;
    lastSel_ = -1;
    panels_.close();
    rise_.restart(0, 1, .32f, easeOut);
    cardsA_.snap(1);
    w_.clear();
    h_.clear();
    scroll_.snap(0);
    ping_ = now();
    dlLoad();
    app().host.send(json{{"downloads", true}});
}

void ControlCenter::host(const json& m) {
    noteIngame(m);
    if (m.value("downloadsChanged", false)) {
        bool isDl = panels_.on() && qsel_ == 3 && crow_ == 1 && panels_.get()->head == "Downloads";
        int keep = isDl ? panels_.get()->sel : 0;
        dlLoad([this, isDl, keep] {
            if (isDl && panels_.on() && panels_.get()->head == "Downloads") panels_.open(downloads(iconX(3), keep));
        });
    }
    if (m.contains("ingame") && m["ingame"].is_boolean() && !m["ingame"].get<bool>() && app().router.isOpen(id)) closeCc(false);
}

void ControlCenter::update(double) {
    panels_.update();
    if (!app().router.isOpen(id)) return;
    if (!app().me.is_object()) {
        closeCc(false);
        return;
    }
    cardsA_.set(panels_.on() ? 0.f : 1.f, .2f, easeCss);
    if (now() - ping_ > 20) {
        ping_ = now();
        app().host.send(json{{"downloads", true}});
    }
}

void ControlCenter::hoverCard(int i) {
    if (panels_.on() || (crow_ == 0 && card_ == i)) return;
    crow_ = 0;
    card_ = i;
}

void ControlCenter::hoverIcon(int i) {
    if (panels_.on() || (crow_ == 1 && qsel_ == i)) return;
    crow_ = 1;
    qsel_ = i;
}

bool ControlCenter::action(const Action& a) {
    if (a.is("ps")) {
        closeCc(true);
        return true;
    }
    if (panels_.on()) return panels_.action(a);
    if (a.is("back")) {
        closeCc(true);
        return true;
    }
    auto cs = cards();
    auto is = icons();
    if (a.is("up") && crow_ == 1) {
        crow_ = 0;
        card_ = std::min(card_, (int)cs.size() - 1);
    } else if (a.is("down") && crow_ == 0) {
        crow_ = 1;
    } else if (a.is("left")) {
        if (crow_ == 0 && card_ > 0) card_--;
        if (crow_ == 1 && qsel_ > 0) qsel_--;
    } else if (a.is("right")) {
        if (crow_ == 0 && card_ < (int)cs.size() - 1) card_++;
        if (crow_ == 1 && qsel_ < (int)is.size() - 1) qsel_++;
    } else if (a.is("ok") || a.is("enter")) {
        if (crow_ == 0 && card_ < (int)cs.size()) cs[card_].run();
        else if (crow_ == 1 && qsel_ < (int)is.size()) is[qsel_].run();
    }
    return true;
}

ANYPS5_SCREEN(ControlCenter, "cc");

}
