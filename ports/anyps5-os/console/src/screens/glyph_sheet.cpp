#include "core/App.h"
#include "core/Glyphs.h"
#include "core/Router.h"
#include "core/Screen.h"
#include "core/Text.h"

namespace ui {

namespace {

class GlyphSheet : public Screen {
public:
    bool action(const Action& a) override {
        if (a.is("back")) app().router.go("home");
        return true;
    }
    std::vector<Hint> legend() const override {
        return {{"Select", "ok"}, {"Back", "back"}, {"Options", "options"}, {"Create", "create"}, {"Control center", "ps"}};
    }
    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, hex(0x0b0f1c));
        static const Source sources[] = {Source::PlayStation, Source::Xbox, Source::Nintendo, Source::Generic, Source::Keyboard, Source::Mouse, Source::Touch};
        static const char* actions[] = {"ok", "back", "square", "triangle", "options", "create", "ps", "l1", "r1", "l2", "r2", "l3", "r3", "zoom", "touchpad", "move", "overview"};
        float x0 = t.r(16), y0 = t.r(8), colW = (t.W() - x0 - t.r(3)) / 17.f, rowH = t.r(6.2f), h = t.r(2.6f);
        text::draw(d, "Button glyphs", t.r(3), t.r(3.4f), {t.r(2.2f), Weight::Light, col::fg, Align::Left, VAlign::Middle});
        for (int c = 0; c < 17; c++)
            text::draw(d, actions[c], x0 + colW * (c + .5f), y0 - t.r(1.6f), {t.r(1.05f), Weight::Regular, col::dim, Align::Center, VAlign::Middle});
        for (int r = 0; r < 7; r++) {
            float cy = y0 + rowH * (r + .5f);
            d.rect({t.r(3), cy - rowH / 2, t.W() - t.r(6), t.r(.06f)}, col::line);
            text::draw(d, sourceLabel(sources[r]), t.r(3), cy, {t.r(1.3f), Weight::Medium, col::fg, Align::Left, VAlign::Middle});
            for (int c = 0; c < 17; c++) {
                float w = glyph::width(actions[c], h, sources[r]);
                if (w > colW - t.r(.3f)) {
                    float hs = h * (colW - t.r(.3f)) / w;
                    glyph::draw(d, actions[c], x0 + colW * c + t.r(.15f), cy, hs, col::fg, sources[r]);
                } else if (w > 0) {
                    glyph::draw(d, actions[c], x0 + colW * (c + .5f) - w / 2, cy, h, col::fg, sources[r]);
                }
            }
        }
        float y = y0 + rowH * 7 + t.r(1.6f);
        TextStyle st{t.r(1.5f), Weight::Regular, col::fg, Align::Left, VAlign::Middle};
        glyph::line(d, "Now: press {ok} to select, {back} to go back, {ps} for the control center.", t.r(3), y, st);
        text::draw(d, glyph::fill("In words: {ok} selects, {back} goes back, {ps} opens the control center, {options} opens options."), t.r(3), y + t.r(2.8f),
                   {t.r(1.3f), Weight::Regular, col::dim, Align::Left, VAlign::Middle});
    }
};

ANYPS5_SCREEN(GlyphSheet, "glyphs");

}

}
