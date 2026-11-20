#include "core/Glyphs.h"
#include "core/App.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include <cmath>
#include <random>

namespace ui {

class Boot : public Screen {
public:
    void enter() override {
        ready_.restart(0, 1, 1.2f, easeOut, .2f);
        text_.restart(0, 1, 1.2f, easeCss, .8f);
    }

    bool action(const Action& a) override {
        if (a.text.size() && a.name == "text") return true;
        App& A = app();
        json auto_ = A.settings.get("autoLogin", "");
        if (auto_.is_string() && !auto_.get<std::string>().empty()) {
            json* p = A.settings.profileByUid(auto_.get<std::string>());
            if (p && p->value("pinHash", std::string()).empty()) {
                A.enterHome(*p);
                return true;
            }
        }
        A.router.go("users");
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        Rect full{0, 0, t.W(), t.H()};
        SDL_Texture* bg = d.cached("boot-bg", t.w, t.h, [&](Draw& c) { paintBackground(c, t); });
        d.image(bg, full, Fit::Fill);
        pointer::target(full, nullptr, [] { app().act({"ok", ""}); });
        SDL_Texture* bokeh = d.cached("boot-bokeh", t.w, t.h, [&](Draw& c) { paintBokeh(c, t); });
        double cyc = std::fmod(now(), 80.0);
        float k = (float)(cyc < 40 ? cyc / 40 : (80 - cyc) / 40);
        float s = 1 + .03f * k;
        Rect br{t.W() * (1 - s) / 2 - t.W() * .02f * k, t.H() * (1 - s) / 2, t.W() * s, t.H() * s};
        d.image(bokeh, br, Fit::Fill);

        float ta = text_.value();
        Source pad = glyph::pad();
        bool chord = pad == Source::Generic && !app().input.padGuide();
        glyph::line(d, app().quest() ? "Press {ok}, or point at the screen and pull the trigger." : chord ? "Press {ok} on your controller, or any key." : "Press {ps} on your controller, or any key.", t.W() / 2, t.r(15.3f),
                    {t.r(1.85f), Weight::Light, white(.88f * ta), Align::Center, VAlign::Middle}, pad);

        float r = ready_.value();
        float sc = .9f + .1f * r;
        float dia = t.r(5.25f) * sc;
        float cx = t.W() / 2, cy = t.r(35);
        d.pushAlpha(r);
        d.dot(cx, cy, dia / 2 + t.r(3), white(.18f), white(0));
        d.circle(cx, cy, dia / 2, white());
        d.icon(images::asset("logo-mark.png"), cx, cy, t.r(3.3f) * sc, black());
        d.popAlpha();
    }

private:
    static void paintBackground(Draw& c, const Theme& t) {
        float W = t.W(), H = t.H();
        Color in = hex(0x1e2233), out = hex(0x08090e);
        float cx = W * .5f, cy = H * 1.25f, rx = W * 1.3f, ry = H * .9f;
        c.field({0, 0, W, H}, 64, 36, [&](float x, float y) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            float g = std::sqrt(dx * dx + dy * dy) / .58f;
            return mix(in, out, std::min(1.f, g));
        });
        float ax = W * .14f, ay = -H * .18f;
        const float st[][2] = {{0, 0}, {7, .13f}, {13, 0}, {19, .08f}, {26, 0}, {33, .05f}, {40, 0}};
        Color ray = rgba(255, 236, 214, 1);
        c.field({0, 0, W, H}, 160, 90, [&](float x, float y) {
            float ang = std::atan2(x - ax, -(y - ay)) * 180.f / 3.14159265f;
            if (ang < 0) ang += 360;
            float rel = ang - 150;
            float a = 0;
            if (rel >= 0 && rel <= 40)
                for (int i = 1; i < 7; i++)
                    if (rel <= st[i][0]) {
                        float k = (rel - st[i - 1][0]) / (st[i][0] - st[i - 1][0]);
                        a = st[i - 1][1] + (st[i][1] - st[i - 1][1]) * k;
                        break;
                    }
            return alpha(ray, a);
        });
    }

    static void paintBokeh(Draw& c, const Theme& t) {
        std::mt19937 rng(20251115);
        std::uniform_real_distribution<float> U(0, 1);
        float sx = t.W() / 1920.f, sy = t.H() / 1080.f;
        for (int i = 0; i < 460; i++) {
            float px = U(rng) * 1920, band = 520 + (px - 960) * -0.12f;
            float py = band + (U(rng) - .5f) * 360 * U(rng);
            float r = U(rng) < .15f ? 6 + U(rng) * 10 : 1 + U(rng) * 3.5f;
            float a = .18f + U(rng) * .6f;
            int g = 170 + (int)(U(rng) * 60), b = 110 + (int)(U(rng) * 50);
            c.dot(px * sx, py * sy, std::max(1.f, r * sx), rgba(255, g, b, a), rgba(255, 180, 120, 0));
        }
    }

    Tween ready_, text_;
};

ANYPS5_SCREEN(Boot, "boot");

}
