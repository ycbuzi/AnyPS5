#include "core/App.h"
#include "core/Glyphs.h"
#include "core/Router.h"
#include "core/Screen.h"
#include "core/Text.h"
#include <algorithm>

namespace ui {

namespace {

struct HelpRow {
    std::string keys, what;
};

struct HelpSection {
    std::string head;
    std::vector<HelpRow> rows;
};

std::vector<HelpSection> sections(Source src) {
    Source b = isPointer(src) ? app().input.buttonSource : src;
    bool pad = isPad(b);
    bool chord = pad && b == Source::Generic && !app().input.padGuide();
    std::vector<HelpSection> s;
    if (app().quest()) {
        s.push_back({"In AnyPS5: Quest Touch controllers",
            {{"Point a controller at the screen", "Move the pointer"},
             {"Pull its trigger", "Select what you point at"},
             {"{move}", "Move the selection"},
             {"{ok}", "Select"},
             {"{back}", "Back"},
             {"{ps}", "Control center"},
             {"{options}", "Options"},
             {"{l1} {r1}", "Switch tabs"}}});
        s.push_back({"The headset",
            {{"Hold the Meta button", "Recenter the screen in front of you"},
             {"Press the Meta button", "Quest menu; AnyPS5 keeps running"}}});
        return s;
    }
    if (isPointer(src)) {
        bool touch = src == Source::Touch;
        s.push_back({touch ? "Touchscreen" : "Mouse and touchpad",
            {{touch ? "Tap" : "Click, or tap the touchpad", "Select"},
             {touch ? "Tap a button in the bottom row" : "Right-click, or two-finger tap", "Back"},
             {touch ? "Swipe" : "Wheel, or two-finger swipe", "Scroll"},
             {touch ? "Tap Options in the bottom row" : "Middle-click", "Options"}}});
    }
    std::string head = std::string("In AnyPS5: ") + sourceLabel(b);
    if (pad)
        s.push_back({head,
            {{"{move} or the left stick", "Move"},
             {"{ok}", "Select"},
             {"{back}", "Back"},
             {chord ? "{ps} together" : "{ps}", "Control center"},
             {"{options}", "Options"},
             {"{create}", "Create"},
             {"{l1} {r1}", "Switch tabs"},
             {"{zoom}", "Zoom (when it is on in Accessibility)"}}});
    else
        s.push_back({head,
            {{"{move} or W A S D", "Move"},
             {"{ok} or Space", "Select"},
             {"{back} or Backspace", "Back"},
             {app().input.overview ? "{ps} or Q" : "{ps}", "Control center"},
             {"{options}", "Options"},
             {"{create}", "Create"},
             {"{help} or ?", "Controls and help"},
             {"{l1} {r1} or PgDn", "Switch tabs"}}});
    std::string quit = pad ? (chord ? "Hold {ps} for half a second" : "{ps}") : app().input.overview ? "{overview} or Ctrl+Alt+Esc" : "Ctrl+Alt+Esc";
    s.push_back({"During a game",
        {{quit, "Quit the game and go back to AnyPS5"}, {"Keep holding for 1 second", "Close the game straight away"}}});
    s.push_back({"Other devices",
        {{"PlayStation, Xbox, Nintendo, other pads", "All work. These hints follow the device you used last"},
         {"A pad without a Home button", "Start + Select together is the Home button"}}});
    return s;
}

class Help : public Screen {
public:
    bool overlay() const override { return true; }
    bool action(const Action& a) override {
        if (a.is("back") || a.is("ps") || a.is("help") || a.is("ok")) app().router.close(id);
        return true;
    }
    std::vector<Hint> legend() const override { return {{"Back", "back"}}; }
    void draw(Draw& d) override {
        const Theme& t = app().theme;
        Source src = app().input.source;
        auto secs = sections(src);
        d.rect({0, 0, t.W(), t.H()}, black(.6f));
        Font fh{t.r(1.4f), Weight::Semibold}, fk{t.r(1.4f), Weight::Medium}, fw{t.r(1.4f), Weight::Regular}, ft{t.r(2.2f), Weight::Light};
        Source gs = isPointer(src) ? app().input.buttonSource : src;
        float rowH = t.r(isPointer(src) ? 2.5f : 2.75f), headH = t.r(3.2f);
        float h = t.r(3) + text::lineHeight(ft) + t.r(1.2f);
        for (const HelpSection& s : secs) h += headH + rowH * s.rows.size();
        float w = std::min(t.r(74), t.W() - t.r(4));
        Rect p{(t.W() - w) / 2, std::max(t.r(1), (t.H() - h) / 2), w, h};
        d.round(p, t.r(1), hex(0x10141f, .97f));
        d.stroke(p, t.r(1), t.r(.06f), col::line);
        float x = p.x + t.r(2.4f), kw = w * .46f, y = p.y + t.r(2.2f);
        text::draw(d, "Controls and help", x, y, {ft.size, ft.weight, col::fg, Align::Left, VAlign::Top});
        text::draw(d, std::string("Showing: ") + sourceLabel(src), p.x + w - t.r(2.4f), y + text::lineHeight(ft) / 2,
                   {t.r(1.2f), Weight::Regular, col::dim, Align::Right, VAlign::Middle});
        y += text::lineHeight(ft) + t.r(1.2f);
        for (const HelpSection& s : secs) {
            text::draw(d, s.head, x, y + t.r(1.1f), {fh.size, fh.weight, col::dim, Align::Left, VAlign::Top, 0, true});
            y += headH;
            for (const HelpRow& r : s.rows) {
                d.rect({x, y, w - t.r(4.8f), t.r(.06f)}, col::line);
                float cy = y + rowH / 2;
                glyph::line(d, r.keys, x, cy, {fk.size, fk.weight, col::fg, Align::Left, VAlign::Middle}, gs);
                text::draw(d, text::ellipsize(r.what, fw, w - kw - t.r(4.8f)), x + kw, cy, {fw.size, fw.weight, col::dim, Align::Left, VAlign::Middle});
                y += rowH;
            }
        }
    }
};

ANYPS5_SCREEN(Help, "help");

}

}
