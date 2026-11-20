#include "screens/control_center.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::cc {

namespace {
std::string ingameBg;
bool dnd = false;
}

void PanelHost::open(std::unique_ptr<Panel> p) {
    if (panel_) old_.push_back(std::move(panel_));
    panel_ = std::move(p);
    in_.restart(0, 1, .17f, easeOut);
    dim_.set(1, .12f, easeCss);
}

void PanelHost::close() {
    if (!panel_) return;
    old_.push_back(std::move(panel_));
    dim_.set(0, .12f, easeCss);
}

bool PanelHost::action(const Action& a) {
    if (!panel_) return false;
    if (a.is("ps")) return false;
    if (panel_->key(a)) return true;
    Panel* p = panel_.get();
    if (a.is("up") && p->sel > 0) {
        p->sel--;
        p->moved();
    } else if (a.is("down") && p->sel < p->count() - 1) {
        p->sel++;
        p->moved();
    } else if (a.is("ok") || a.is("enter")) {
        Run r = p->pick();
        close();
        if (r) r();
    } else if (a.is("back")) {
        close();
    }
    return true;
}

void PanelHost::update() { old_.clear(); }

Rect PanelHost::box() const {
    const Theme& t = app().theme;
    if (!panel_) return {0, 0, 0, 0};
    float w = panel_->width(), h = panel_->height();
    if (panel_->place == Place::Above) {
        Rect a = panel_->anchor;
        float x = std::min(t.W() - w - t.r(1), std::max(t.r(1), a.x));
        float below = a.y + a.h + t.r(.625f);
        float y = below + h < t.H() ? below : std::max(t.r(1), a.y - h - t.r(.625f));
        return {x, y, w, h};
    }
    if (panel_->place == Place::Center) return {(t.W() - w) / 2, (t.H() - h) / 2, w, h};
    float ax = panel_->anchorX < 0 ? t.W() / 2 : panel_->anchorX;
    float x = std::min(t.W() - w - t.r(5), std::max(t.r(5), ax - w / 2));
    float y = std::max(t.r(2), t.H() - t.r(11.9f) - h);
    return {x, y, w, h};
}

void PanelHost::draw(Draw& d) {
    const Theme& t = app().theme;
    float dm = dim_.value();
    if (dm > 0) d.rect({0, 0, t.W(), t.H()}, black(.55f * dm));
    if (!panel_) return;
    Rect r = box();
    pointer::target({0, 0, t.W(), t.H()}, nullptr, [] { app().act({"back", ""}); });
    pointer::target(r, nullptr, nullptr, panel_->radius());
    pointer::scrollArea(r, [this](float rows) {
        wheel_ += rows;
        while (wheel_ >= 1) {
            wheel_ -= 1;
            action({"down", ""});
        }
        while (wheel_ <= -1) {
            wheel_ += 1;
            action({"up", ""});
        }
    });
    float a = in_.value();
    if (a < 1) {
        d.beginLayer();
        drawPanelFrame(d, r, panel_->radius(), panel_->background());
        panel_->draw(d, r);
        d.endLayer(a, 1, 0, t.r(.7f) * (1 - a));
        return;
    }
    drawPanelFrame(d, r, panel_->radius(), panel_->background());
    panel_->draw(d, r);
}

void drawPanelFrame(Draw& d, Rect r, float radius, Color bg) {
    const Theme& t = app().theme;
    if (t.highContrast) bg = black();
    d.shadow(r, radius, t.r(3), black(.6f), 0, t.r(1));
    d.round(r, radius, bg);
}

void drawToggle(Draw& d, float x, float cy, bool on) {
    const Theme& t = app().theme;
    Rect r{x, cy - t.r(1), t.r(3.6f), t.r(2)};
    d.round(r, t.r(1), white(on ? .6f : .25f));
    float k = t.r(1.5f);
    d.circle(x + (on ? t.r(1.85f) : t.r(.25f)) + k / 2, cy, k / 2, white());
}

void drawRowSel(Draw& d, Rect r) {
    const Theme& t = app().theme;
    pointer::target(r, nullptr, [] { app().act({"ok", ""}); }, t.r(.2f));
    d.round(r, t.r(.2f), white(.07f));
    d.stroke(r, t.r(.2f), t.r(t.highContrast ? .22f : .1f), white());
}

void drawThumb(Draw& d, Rect r, float radius, const std::string& url, Color bg) {
    d.round(r, radius, bg);
    SDL_Texture* tex = url.empty() ? nullptr : images::get(url);
    if (tex) d.image(tex, r, Fit::Cover, 1, radius);
}

bool playing(Playing& out) {
    std::string p = app().playing();
    if (p.empty()) return false;
    const host::Game* g = app().playingGame();
    if (g) out = {g->title, g->id, g->icon, g->bg, g->fps};
    else out = {p, "", "", "", 0};
    return true;
}

void noteIngame(const json& m) {
    if (!m.contains("ingame") || !m["ingame"].is_boolean()) return;
    ingameBg = m["ingame"].get<bool>() && m.contains("bg") && m["bg"].is_string() ? m["bg"].get<std::string>() : std::string();
}

void drawIngame(Draw& d) {
    if (!app().ingame) return;
    const Theme& t = app().theme;
    Rect all{0, 0, t.W(), t.H()};
    d.rect(all, black());
    SDL_Texture* tex = ingameBg.empty() ? nullptr : images::get(ingameBg);
    if (tex) d.image(tex, all, Fit::Contain);
    d.rect(all, black(.12f));
}

bool dndOn() { return dnd; }

void setDnd(bool on) {
    dnd = on;
    app().host.send(json{{"dnd", on}});
    json pop = app().settings.noteOpt("popups", true);
    app().toasts.popups = !on && (!pop.is_boolean() || pop.get<bool>());
}

void closeAll() {
    for (const char* id : {"cc", "create", "notifications", "viewer"}) app().router.close(id);
}

void enterRest() {
    closeAll();
    app().open("rest");
    app().host.send(json{{"rest", true}});
}

void powerOff(bool restart) {
    closeAll();
    app().toast(restart ? "Restarting AnyPS5..." : "Turning off AnyPS5...");
    app().host.send(json{{"power", restart ? "restart" : "off"}});
}

std::string age(long long wall) {
    long long s = (long long)app().wallNow() - wall;
    if (s < 60) return "Now";
    if (s < 3600) return std::to_string(s / 60) + "m";
    if (s < 86400) return std::to_string(s / 3600) + "h";
    return std::to_string(s / 86400) + "d";
}

}
