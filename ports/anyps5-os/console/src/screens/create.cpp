#include "screens/control_center.h"
#include "core/Pointer.h"
#include <algorithm>
#include <ctime>

namespace ui::cc {

namespace {

std::string when(double ms) {
    std::time_t t = (std::time_t)(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::strftime(b, sizeof b, app().settings.clock24() ? "%Y-%m-%d %H:%M" : "%Y-%m-%d %I:%M %p", &tm);
    return b;
}

std::string mediaSub(const json& m) {
    std::string s = m.value("source", std::string());
    std::string k = s == "Trophy" ? "Trophy capture" : s == "Test run" ? "Test run" : s == "In game" ? "Screenshot (in the game)" : "Screenshot";
    return k + " \xC2\xB7 " + (m.contains("at") && m["at"].is_number() && m["at"].get<double>() > 0 ? when(m["at"].get<double>()) : "date unknown");
}

json capture(const char* k, const json& d) {
    const json& S = app().settings.S;
    if (S.contains("capture") && S["capture"].is_object() && S["capture"].contains(k)) return S["capture"][k];
    return d;
}

void setCapture(const char* k, const json& v) {
    json& S = app().settings.S;
    if (!S.contains("capture") || !S["capture"].is_object()) S["capture"] = json::object();
    S["capture"][k] = v;
    app().saveSettings();
}

class Create : public Screen {
public:
    bool overlay() const override { return true; }
    void enter() override {
        sel_ = 1;
        hidden_ = false;
        shotAt_ = -1;
        panels_.close();
        app().reloadMedia();
    }
    void update(double) override {
        panels_.update();
        if (shotAt_ >= 0 && now() >= shotAt_) {
            shotAt_ = -1;
            app().host.send(json{{"shot", true}});
        }
        if (app().router.isOpen(id) && !app().me.is_object()) app().router.close(id);
    }
    void host(const json& m) override { noteIngame(m); }
    void close(bool resume) {
        panels_.close();
        app().router.close(id);
        if (resume && app().ingame) app().host.send(json{{"resume", true}});
    }
    static const json* items() {
        auto it = app().media.find("items");
        return it != app().media.end() && it->is_array() && !it->empty() ? &*it : nullptr;
    }
    const json* last() const { return items() ? &(*items())[0] : nullptr; }
    float barTop() const { return app().theme.H() - app().theme.r(16.9f); }
    Rect lastRect() const { return {app().theme.r(.4f), barTop() + app().theme.r(4), app().theme.r(15.3f), app().theme.r(8.6f)}; }
    float btnX(int i) const {
        const Theme& t = app().theme;
        return i == 1 ? t.W() * .479f : t.W() - t.r(7.6f) - t.r(2.2f);
    }
    Rect selRect() const {
        const Theme& t = app().theme;
        if (sel_ == 0) return lastRect();
        float s = t.r(4.4f);
        return {btnX(sel_) - s / 2, barTop() + t.r(5.7f), s, s};
    }
    void hover(int i) { sel_ = i; }
    void pick() {
        if (sel_ == 0) {
            if (!items()) {
                app().toast("No captures yet. Take a screenshot first.");
                return;
            }
            const json& it = *items();
            json items = json::array();
            for (size_t i = 0; i < it.size() && i < 40; i++) {
                const json& m = it[i];
                std::string game = m.value("game", std::string());
                std::string title = m.contains("trophy") && m["trophy"].is_object() ? m["trophy"].value("name", std::string("Trophy")) + " \xC2\xB7 " + game : game;
                items.push_back({{"img", m.value("src", std::string())}, {"title", title}, {"sub", mediaSub(m)}, {"mid", m.value("id", std::string())}});
            }
            app().viewer = {{"items", items}, {"index", 0}};
            app().open("viewer");
            return;
        }
        if (sel_ == 1) {
            bool was = app().ingame;
            hidden_ = true;
            close(true);
            shotAt_ = now() + (was ? .12 : .1);
            return;
        }
        std::string fmt = capture("fmt", "png").is_string() ? capture("fmt", "png").get<std::string>() : "png";
        bool tr = capture("trophy", true).is_boolean() ? capture("trophy", true).get<bool>() : true;
        bool nt = capture("notify", true).is_boolean() ? capture("notify", true).get<bool>() : true;
        const std::string tick = "\xE2\x9C\x93  ";
        panels_.open(psMenu({{(fmt == "png" ? tick : "") + "Screenshot File Type: PNG", [] { setCapture("fmt", "png"); }},
                             {(fmt == "jpg" ? tick : "") + "Screenshot File Type: JPG", [] { setCapture("fmt", "jpg"); }},
                             {(tr ? tick : "") + "Save Trophy Screenshots", [tr] { setCapture("trophy", !tr); }, true},
                             {(nt ? tick : "") + "Show a Notification When Saved", [nt] { setCapture("notify", !nt); }},
                             {"Cancel", {}, true}},
                            selRect()));
    }
    bool action(const Action& a) override {
        if (a.is("ps")) {
            close(false);
            app().open("cc");
            return true;
        }
        if (panels_.on()) return panels_.action(a);
        if (a.is("left") && sel_ > 0) sel_--;
        else if (a.is("right") && sel_ < 2) sel_++;
        else if (a.is("ok") || a.is("enter")) pick();
        else if (a.is("back") || a.is("create")) close(true);
        return true;
    }
    void draw(Draw& d) override {
        if (hidden_) return;
        const Theme& t = app().theme;
        drawIngame(d);
        pointer::target({0, 0, t.W(), t.H()}, nullptr, [] { app().act({"back", ""}); });
        float top = barTop();
        pointer::target({0, top, t.W(), t.H() - top}, nullptr, nullptr);
        d.linear({0, top, t.W(), t.H() - top}, 180, {{0, rgba(6, 7, 11, .95f)}, {1, rgba(6, 7, 11, .99f)}});
        Rect lr = lastRect();
        const json* m = last();
        drawThumb(d, lr, t.r(.3f), m ? m->value("thumb", std::string()) : "", hex(0x20232c));
        Font fe{t.r(1.25f), Weight::Regular};
        if (m) {
            std::string g = text::ellipsize(m->value("game", std::string()), fe, lr.w - t.r(1.9f));
            if (!g.empty()) {
                float gw = text::width(g, fe) + t.r(.9f), gh = text::lineHeight(fe) + t.r(.1f);
                Rect e{lr.x + lr.w - t.r(.5f) - gw, lr.y + lr.h - t.r(.4f) - gh, gw, gh};
                d.round(e, t.r(.2f), black(.6f));
                text::draw(d, g, e.x + e.w / 2, e.y + e.h / 2, {fe.size, fe.weight, white(), Align::Center, VAlign::Middle});
            }
        } else {
            text::draw(d, "No captures yet", lr.x + lr.w / 2, lr.y + lr.h / 2, {fe.size, fe.weight, white(.6f), Align::Center, VAlign::Middle});
        }
        float s = t.r(4.4f), cy = top + t.r(5.7f) + s / 2;
        if (!panels_.on()) {
            pointer::target(lr, [this] { hover(0); }, [this] { hover(0); pick(); }, t.r(.3f));
            for (int i = 1; i < 3; i++)
                pointer::target({btnX(i) - s / 2, top + t.r(5.7f), s, s}, [this, i] { hover(i); }, [this, i] { hover(i); pick(); }, s / 2);
        }
        Rect sr = selRect();
        float rad = sel_ == 0 ? t.r(.3f) : s / 2;
        d.soft(sr, rad, t.r(.5f), white(.2f));
        if (sel_ > 0) d.circle(sr.x + s / 2, cy, s / 2, white(.12f));
        d.outline(sr, rad, t.r(.15f), white());
        const char* names[3] = {"", "cr-shot", "cr-opts"};
        for (int i = 1; i < 3; i++)
            if (SDL_Texture* g = images::icon(names[i], t.r(2.5f))) d.icon(g, btnX(i), cy, t.r(2.5f));
        const char* labels[3] = {"Recent Captures", "Take Screenshot", "Capture Options"};
        Font fl{t.r(1.5f), Weight::Regular};
        float lw = text::width(labels[sel_], fl);
        float lx = std::max(sr.x + sr.w / 2, lw / 2 + 8);
        text::draw(d, labels[sel_], lx, top + t.r(1.9f), {fl.size, fl.weight, white(), Align::Center, VAlign::Top});
        panels_.draw(d);
    }

private:
    int sel_ = 1;
    bool hidden_ = false;
    double shotAt_ = -1;
    PanelHost panels_;
};

ANYPS5_SCREEN(Create, "create");

}

}
