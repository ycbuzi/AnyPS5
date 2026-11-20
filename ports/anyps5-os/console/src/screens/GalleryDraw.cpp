#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/Gallery.h"
#include <algorithm>

namespace ui {

namespace {

bool narrow() {
    const Theme& t = app().theme;
    return t.W() * 3 <= t.H() * 4;
}

int columns() { return narrow() ? 2 : 3; }

Rect gridRect() {
    const Theme& t = app().theme;
    if (narrow()) return {t.r(3), t.r(21.4f), t.W() - t.r(6), t.H() - t.r(3) - t.r(21.4f)};
    return {t.r(10.2f), t.r(20.9f), t.W() - t.r(10.2f) - t.r(10.4f), t.H() - t.r(3) - t.r(20.9f)};
}

TextStyle style(float size, Weight w, Color c, Align a = Align::Left, VAlign v = VAlign::Middle) { return {size, w, c, a, v}; }

}

Rect Gallery::tileRect(int i) const {
    const Theme& t = app().theme;
    Rect g = gridRect();
    int cols = columns();
    float pad = t.r(.2f), cg = t.r(1.75f), rg = t.r(2);
    float w = (g.w - 2 * pad - (cols - 1) * cg) / cols, h = w * 9 / 16;
    return {g.x + pad + (i % cols) * (w + cg), g.y + pad + (i / cols) * (h + rg) - scroll_, w, h};
}

void Gallery::draw(Draw& d) {
    const Theme& t = app().theme;
    kit::setbg(d);
    auto list = items();
    if (list.empty() && focus_ == "grid") focus_ = album_.empty() ? "tabs" : "side";
    if (sel_ >= (int)list.size()) sel_ = std::max(0, (int)list.size() - 1);

    std::string head = "Media Gallery";
    if (album_ == "_tests") head = "Test Runs";
    else if (!album_.empty()) head = !list.empty() && !list[0].game.empty() ? list[0].game : std::string("Album");
    TextStyle hs = style(t.r(2.85f), Weight::Light, white());
    text::draw(d, text::ellipsize(head, hs.font(), t.r(70)), narrow() ? t.r(3) : t.r(6), t.r(6.4f), hs);

    if (album_.empty()) drawTabs(d);
    if (!albumView()) {
        TextStyle sl = style(t.r(1.3f), Weight::Regular, white(.7f), narrow() ? Align::Left : Align::Right);
        std::string label = "Sort by: " + gallerySortName(gallerySort());
        float lw = text::width(label, sl.font()), lh = text::lineHeight(sl.font());
        float lx = narrow() ? t.r(3) : t.W() - t.r(10.4f) - lw, ly = narrow() ? t.r(18.6f) : t.r(18.1f);
        text::draw(d, label, narrow() ? lx : lx + lw, ly, sl);
        pointer::target({lx - t.r(.6f), ly - lh / 2 - t.r(.4f), lw + t.r(1.2f), lh + t.r(.8f)}, nullptr, [this] {
            focus_ = "side";
            menu();
        }, t.r(.2f));
    }

    float bs = t.r(3.6f);
    sortBtn_ = narrow() ? Rect{t.W() - t.r(3) - bs, t.r(15.5f), bs, bs} : Rect{t.r(5.1f), t.r(21), bs, bs};
    bool sideSel = focus_ == "side";
    if (sideSel) kit::glow(d, sortBtn_, bs / 2);
    d.circle(sortBtn_.x + bs / 2, sortBtn_.y + bs / 2, bs / 2, white(sideSel ? .18f : .1f));
    d.icon(images::icon("sort", t.r(1.8f)), sortBtn_.x + bs / 2, sortBtn_.y + bs / 2, t.r(1.8f));
    pointer::target(sortBtn_, [this] { focus_ = "side"; }, [this] {
        focus_ = "side";
        menu();
    }, bs / 2);

    drawGrid(d, list);
    menu_.draw(d);
}

void Gallery::drawTabs(Draw& d) {
    const Theme& t = app().theme;
    static const char* labels[][2] = {{"all", "All"}, {"fav", "Favorites"}, {"trophies", "Trophies"}, {"albums", "Albums"}};
    TextStyle st = style(t.r(1.6f), Weight::Regular, white(.5f));
    float x = narrow() ? t.r(3) : t.r(10.2f), cy = t.r(11.1f);
    float h = text::lineHeight(st.font()) + t.r(.9f);
    int idx = 0;
    for (auto& l : labels) {
        bool on = tab_ == l[0];
        float w = text::width(l[1], st.font()) + t.r(2.2f);
        Rect r{x, cy - h / 2, w, h};
        if (on && focus_ == "tabs") {
            kit::glow(d, r, t.r(.25f));
            d.round(r, t.r(.25f), white(.1f));
        }
        TextStyle s = st;
        s.color = on ? white() : white(.5f);
        text::draw(d, l[1], x + t.r(1.1f), cy, s);
        pointer::target(r, [this] { focus_ = "tabs"; }, [this, idx] {
            focus_ = "tabs";
            setTab(idx);
        }, t.r(.25f));
        idx++;
        x += w + t.r(1.2f);
    }
}

void Gallery::drawGrid(Draw& d, const std::vector<GalleryItem>& list) {
    const Theme& t = app().theme;
    Rect g = gridRect();
    if (list.empty()) {
        std::string msg = tab_ == "fav" && album_.empty() ? "No favorites yet. Press Options on a capture and add it to Favorites."
                          : tab_ == "trophies" && album_.empty() ? "No trophy captures yet. AnyPS5 saves one the moment you earn a trophy in a game."
                                                                 : "No captures yet. Use Screenshot in the control center while you play.";
        TextStyle em = style(t.r(1.6f), Weight::Regular, white(.65f), Align::Left, VAlign::Top);
        auto lines = text::wrap(msg, em.font(), g.w - t.r(.8f));
        text::drawLines(d, lines, g.x + t.r(.4f), g.y + t.r(1.2f), t.r(2.4f), em);
        return;
    }
    float saved = scroll_;
    scroll_ = 0;
    Rect s0 = tileRect(sel_), last = tileRect((int)list.size() - 1);
    float pitch = tileRect(columns()).y - tileRect(0).y;
    bool loose = pointer::active();
    float top = loose ? saved : s0.y - g.y;
    scroll_ = kit::scrollNearest(saved, top, loose ? top : s0.y - g.y + s0.h, g.h, last.y + last.h + t.r(.2f) - g.y);
    pointer::scrollArea(g, [this, pitch](float rows) { scroll_ += rows * pitch; });
    auto fav = favs();
    d.pushClip(g);
    for (size_t i = 0; i < list.size(); i++) {
        Rect r = tileRect((int)i);
        if (r.y > g.y + g.h || r.y + r.h < g.y) continue;
        drawTile(d, list[i], r, focus_ == "grid" && (int)i == sel_, fav);
        int n = (int)i;
        pointer::target(r, [this, n] {
            focus_ = "grid";
            sel_ = n;
        }, [this, n] {
            focus_ = "grid";
            sel_ = n;
            ok();
        }, t.r(.3f));
    }
    d.popClip();
}

void Gallery::drawTile(Draw& d, const GalleryItem& x, Rect r, bool sel, const std::vector<std::string>& fav) {
    const Theme& t = app().theme;
    float rad = t.r(.3f);
    if (sel) kit::glow(d, r, rad);
    d.round(r, rad, hex(0x20232c));
    d.image(images::get(x.thumb), r, Fit::Cover, 1.f, rad);
    d.pushClip(r);
    if (x.album) {
        TextStyle b = style(t.r(1.6f), Weight::Regular, white(), Align::Left, VAlign::Top);
        TextStyle sm = style(t.r(1.25f), Weight::Regular, white(.7f), Align::Left, VAlign::Top);
        float hb = text::lineHeight(b.font()), hsm = text::lineHeight(sm.font());
        float gh = t.r(1.6f) + hb + hsm + t.r(.7f);
        d.linear({r.x, r.y + r.h - gh, r.w, gh}, 180, {{0, black(0)}, {1, black(.75f)}});
        float ty = r.y + r.h - t.r(.7f) - hsm - hb;
        text::draw(d, text::ellipsize(x.game, b.font(), r.w - t.r(1.8f)), r.x + t.r(.9f), ty, b);
        text::draw(d, std::to_string(x.count) + (x.count == 1 ? " item" : " items"), r.x + t.r(.9f), ty + hb, sm);
    } else {
        TextStyle gg = style(t.r(1.25f), Weight::Regular, white(), Align::Left, VAlign::Top);
        float hg = text::lineHeight(gg.font());
        d.linear({r.x, r.y, r.w, t.r(.6f) + hg + t.r(1.6f)}, 180, {{0, black(.65f)}, {1, black(0)}});
        text::draw(d, text::ellipsize(x.game, gg.font(), r.w - t.r(1.6f)), r.x + t.r(.8f), r.y + t.r(.6f), gg);
        bool hearted = std::find(fav.begin(), fav.end(), x.id) != fav.end();
        bool cup = !x.trophyGrade.empty();
        float bh = cup ? t.r(1.9f) : t.r(1.7f), bx = r.x + r.w - t.r(.7f), by = r.y + t.r(.55f) + bh / 2;
        if (hearted) {
            d.icon(images::icon("heart", t.r(1.7f)), bx - t.r(.85f), by, t.r(1.7f));
            bx -= t.r(1.7f) + t.r(.5f);
        }
        if (cup) kit::cup(d, x.trophyGrade, bx - t.r(.95f), by, t.r(1.9f));
        if (x.source == "Test run") {
            TextStyle tt = style(t.r(1.25f), Weight::Regular, white(), Align::Left, VAlign::Top);
            float lw = text::width("Test run", tt.font()) + t.r(1), lh = text::lineHeight(tt.font()) + t.r(.2f);
            Rect lb{r.x + t.r(.7f), r.y + r.h - t.r(.6f) - lh, lw, lh};
            d.round(lb, t.r(.2f), black(.6f));
            text::draw(d, "Test run", lb.x + t.r(.5f), lb.y + t.r(.1f), tt);
        }
    }
    d.popClip();
}

}
