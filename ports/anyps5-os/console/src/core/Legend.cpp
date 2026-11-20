#include "core/App.h"
#include "core/Glyphs.h"
#include "core/Pointer.h"
#include "core/Text.h"

namespace ui {

void App::drawLegend(Draw& d) {
    Screen* t = router.top();
    if (!t) return;
    auto hints = t->legend();
    if (hints.empty()) return;
    Source src = input.source;
    bool ptr = isPointer(src);
    TextStyle lab{theme.r(1.25f), Weight::Regular, col::dim, Align::Left, VAlign::Middle};
    float gap = theme.r(ptr ? 1.1f : 1.6f), badgeH = theme.r(2), mr = theme.r(.4f), chipPad = theme.r(.95f);
    float chipH = theme.r(src == Source::Touch ? 2.5f : 2.1f);
    struct Item {
        std::string key, label;
        float gw, lw, w;
        bool chip;
    };
    std::vector<Item> items;
    float total = 0;
    for (auto& h : hints) {
        if (src == Source::Quest && !glyph::has(h.key, src)) continue;
        bool word = ptr && (h.key == "ok" || h.key == "enter");
        bool chip = ptr && !word;
        float gw = chip ? 0 : glyph::width(h.key, badgeH, src);
        float lw = text::width(h.label, lab.font());
        float w = chip ? lw + 2 * chipPad : gw + (gw > 0 ? mr : 0) + lw;
        items.push_back({h.key, h.label, gw, lw, w, chip});
        total += w;
    }
    if (items.empty()) return;
    total += gap * (items.size() - 1);
    float x = theme.W() - theme.r(2.4f) - total;
    float cy = theme.H() - theme.r(1.2f) - badgeH / 2;
    for (auto& i : items) {
        if (i.chip) {
            Rect c{x, cy - chipH / 2, i.w, chipH};
            d.round(c, chipH / 2, white(.08f));
            d.stroke(c, chipH / 2, theme.r(.08f), col::faint);
            text::draw(d, i.label, x + i.w / 2, cy, {lab.size, Weight::Medium, white(.9f), Align::Center, VAlign::Middle});
        } else {
            if (i.gw > 0) glyph::draw(d, i.key, x, cy, badgeH, col::fg, src);
            text::draw(d, i.label, x + i.gw + (i.gw > 0 ? mr : 0), cy, lab);
        }
        float th = std::max(badgeH, chipH) + theme.r(.8f);
        pointer::target({x - gap / 2, cy - th / 2, i.w + gap, th}, nullptr, [this, key = i.key] { globalAction({key, ""}); });
        x += i.w + gap;
    }
}

}
