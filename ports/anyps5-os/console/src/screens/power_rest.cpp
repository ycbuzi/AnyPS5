#include "screens/control_center.h"
#include "core/Pointer.h"
#include <cmath>

namespace ui::cc {

namespace {

class Rest : public Screen {
public:
    bool overlay() const override { return true; }
    void enter() override { since_ = now(); }
    void host(const json& m) override {
        if (m.value("woke", false)) wake(true);
    }
    bool action(const Action&) override {
        wake(false);
        return true;
    }
    void wake(bool fromHost) {
        if (!app().router.isOpen(id)) return;
        app().router.close(id);
        if (!fromHost) app().host.send(json{{"wake", true}});
        if (app().ingame) app().host.send(json{{"resume", true}});
    }
    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, black());
        pointer::target({0, 0, t.W(), t.H()}, nullptr, [this] { wake(false); });
        double s = now() - since_;
        bool still = t.reduceMotion;
        float ta = still ? 1.f : s < 3.25 ? 1.f : s >= 5 ? 0.f : (float)(1 - (s - 3.25) / 1.75);
        if (ta > 0) {
            d.pushAlpha(ta);
            Font fb{t.r(2.2f), Weight::Light}, fs{t.r(1.4f), Weight::Regular};
            float hb = text::lineHeight(fb), hs = text::lineHeight(fs), gap = t.r(.8f);
            float y = t.H() / 2 - (hb + gap + hs) / 2;
            text::draw(d, "Rest mode", t.W() / 2, y, {fb.size, fb.weight, white(.75f), Align::Center, VAlign::Top});
            text::draw(d, "Press any button to wake AnyPS5", t.W() / 2, y + hb + gap, {fs.size, fs.weight, white(.55f), Align::Center, VAlign::Top});
            d.popAlpha();
        }
        double ph = std::fmod(s, 2.8) / 2.8;
        float k = still ? 1.f : (float)(.5 - .5 * std::cos(ph * 6.283185307179586));
        float la = .2f + .8f * k;
        float rad = t.r(.55f), cx = t.W() - t.r(3) - rad, cy = t.H() - t.r(3) - rad;
        Color orange = hex(0xff9a2e, la);
        d.soft({cx - rad, cy - rad, 2 * rad, 2 * rad}, rad, t.r(.7f), orange);
        d.circle(cx, cy, rad, orange);
    }

private:
    double since_ = 0;
};

ANYPS5_SCREEN(Rest, "rest");

}

}
