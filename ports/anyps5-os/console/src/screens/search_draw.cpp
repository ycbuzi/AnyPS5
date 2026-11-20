#include "core/Images.h"
#include "core/Pointer.h"
#include "screens/search.h"
#include "widgets/Widgets.h"
#include <algorithm>

namespace ui {

namespace {

struct Geo {
    float left, right, gridX, gridY, gridW, gridH;
};

Geo geo() {
    const Theme& t = app().theme;
    bool narrow = t.W() / t.H() <= 4.f / 3.f;
    Geo g;
    g.left = t.r(narrow ? 3 : 10.75f);
    g.right = t.r(narrow ? 3 : 16.2f);
    g.gridX = narrow ? t.r(3) : t.r(67.4f);
    g.gridY = narrow ? t.r(43) : t.r(15.8f);
    g.gridW = t.W() - g.gridX - g.right;
    g.gridH = t.H() - t.r(3) - g.gridY;
    return g;
}

float tileW() {
    const Theme& t = app().theme;
    return (geo().gridW - t.r(.8f) - 3 * t.r(1)) / 4;
}

float rowH() {
    const Theme& t = app().theme;
    return tileW() + t.r(.5f) + text::lineHeight({t.r(1.25f), Weight::Regular});
}

float histHeadH() {
    const Theme& t = app().theme;
    return text::lineHeight({t.r(1.4f), Weight::Regular}) + t.r(.8f);
}

std::string firstChar(const std::string& s) {
    auto u = osk::decode(s);
    return u.empty() ? "?" : osk::encode(u[0]);
}

}

Rect Search::tileRect(int i) const {
    const Theme& t = app().theme;
    Geo g = geo();
    float pad = t.r(.4f), tw = tileW();
    if (cols() == 1) return {g.gridX + pad, g.gridY + pad + histHeadH() + i * t.r(4.6f) - scroll_, g.gridW - 2 * pad, t.r(4.6f)};
    return {g.gridX + pad + (i % 4) * (tw + t.r(1)), g.gridY + pad + (i / 4) * (rowH() + t.r(1.4f)) - scroll_, tw, tw};
}

void Search::keepVisible(float top, float bottom, float viewH, float contentH) {
    if (!pointer::active()) {
        if (top < scroll_) scroll_ = top;
        else if (bottom > scroll_ + viewH) scroll_ = bottom - viewH;
    }
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH - viewH));
}

void Search::keyTargets(float x, float y) {
    const Theme& t = app().theme;
    float w = t.r(54.4f), padX = t.r(.4f), colW = (w - 2 * padX) / 11;
    float rowTop[6];
    rowTop[0] = y + t.r(.9f);
    for (int i = 1; i <= 4; i++) rowTop[i] = rowTop[i - 1] + t.r(4);
    rowTop[5] = rowTop[4] + t.r(3.9f);
    auto lay = kb_.layout();
    for (int i = 0; i < (int)lay.size(); i++) {
        const osk::Key& k = lay[(size_t)i];
        if (k.r < 0 || k.r > 5) continue;
        float cx0 = x + padX + k.c * colW;
        Rect cell = k.label.empty() ? Rect{cx0, rowTop[k.r], colW, t.r(4)} : Rect{cx0 + t.r(.05f), rowTop[k.r] + (k.r == 4 ? t.r(.5f) : t.r(.1f)), colW * k.w - t.r(.1f), t.r(3.4f)};
        pointer::target(
            cell,
            [this, i] {
                focus_ = "osk";
                oskSel_ = i;
            },
            [this, i] {
                focus_ = "osk";
                oskSel_ = i;
                key(kb_.keyAt(i));
            },
            t.r(.2f));
    }
}

void Search::draw(Draw& d) {
    const Theme& t = app().theme;
    d.rect({0, 0, t.W(), t.H()}, hex(0x15171d));
    osk::pageBackground(d);
    drawTabs(d);
    drawField(d);
    Geo g = geo();
    kb_.draw(d, g.left, t.r(15.8f), oskSel_, focus_ == "osk" && menu_.empty());
    keyTargets(g.left, t.r(15.8f));
    auto r = results();
    if (trimmed().empty()) drawHistory(d, r);
    else drawGrid(d, r);
    if (!menu_.empty()) drawMenu(d);
}

void Search::drawTabs(Draw& d) {
    const Theme& t = app().theme;
    float x = geo().left, cy = t.r(3.9f);
    TextStyle st{t.r(1.6f), Weight::Regular, white(.5f), Align::Left, VAlign::Middle};
    float lh = text::lineHeight(st.font());
    for (int i = 0; i < searchTabCount(); i++) {
        bool on = tab_ == searchTabId(i);
        float w = text::width(searchTabLabel(i), st.font());
        if (on && focus_ == "tabs") {
            Rect b{x - t.r(.7f), cy - lh / 2 - t.r(.3f), w + t.r(1.4f), lh + t.r(.6f)};
            d.round(b, t.r(.2f), white(.07f));
            d.outline(b, t.r(.2f), t.r(.1f), white(.85f));
        }
        st.color = on ? white() : white(.5f);
        text::draw(d, searchTabLabel(i), x, cy, st);
        pointer::target(
            {x - t.r(.7f), cy - lh / 2 - t.r(.3f), w + t.r(1.4f), lh + t.r(.6f)}, [this] { focus_ = "tabs"; },
            [this, i] {
                focus_ = "tabs";
                setTab(searchTabId(i));
            },
            t.r(.2f));
        x += w + t.r(2.6f);
    }
}

void Search::drawField(Draw& d) {
    const Theme& t = app().theme;
    Geo g = geo();
    Rect b{g.left, t.r(7.3f), t.W() - g.left - g.right, t.r(4.4f)};
    d.round(b, t.r(.2f), white(.07f));
    pointer::target(b, nullptr, [this] { focus_ = "osk"; }, t.r(.2f));
    float cy = b.y + b.h / 2, x = b.x + t.r(1.6f);
    d.icon(images::icon("search-field", t.r(1.7f)), x + t.r(.85f), cy, t.r(1.7f));
    x += t.r(1.7f) + t.r(1.1f);
    TextStyle st{t.r(1.85f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    d.pushClip({x, b.y, b.x + b.w - t.r(1.6f) - x, b.h});
    std::string q = field_.str();
    osk::caretText(d, q, field_.cur, x, cy, st, t.r(1.85f) * 1.1f);
    if (q.empty()) {
        st.color = white(.85f);
        text::draw(d, searchPlaceholder(tab_), x + t.r(.22f) + t.r(1.1f), cy, st);
    }
    d.popClip();
}

void Search::drawGrid(Draw& d, const std::vector<SearchResult>& r) {
    const Theme& t = app().theme;
    Geo g = geo();
    float pad = t.r(.4f);
    if (r.empty()) {
        text::draw(d, "Nothing here", g.gridX + pad, g.gridY + pad, {t.r(1.4f), Weight::Regular, col::dim, Align::Left, VAlign::Top});
        return;
    }
    float tw = tileW(), rh = rowH();
    bool focused = focus_ == "grid";
    float content = (((int)r.size() + 3) / 4) * (rh + t.r(1.4f)) + 2 * pad;
    if (focused) {
        float top = (lsel_ / 4) * (rh + t.r(1.4f));
        keepVisible(top, top + rh + 2 * pad, g.gridH, content);
    } else keepVisible(scroll_, scroll_, g.gridH, content);
    pointer::scrollArea({g.gridX, g.gridY, g.gridW, g.gridH}, [this, rh](float rows) { scroll_ += rows * (rh + app().theme.r(1.4f)); });
    d.pushClip({g.gridX - t.r(2), g.gridY, g.gridW + t.r(4), g.gridH});
    TextStyle ls{t.r(1.25f), Weight::Regular, col::dim, Align::Left, VAlign::Top};
    for (int i = 0; i < (int)r.size(); i++) {
        const SearchResult& x = r[(size_t)i];
        Rect c = tileRect(i);
        if (c.y > g.gridY + g.gridH || c.y + rh < g.gridY) continue;
        bool on = focused && i == lsel_;
        float rad = x.round ? tw / 2 : t.r(.8f);
        if (on) widgets::focusGlow(d, c, rad);
        if (x.round) widgets::avatar(d, c.x + tw / 2, c.y + tw / 2, tw, x.player, t.r(3), Weight::Bold);
        else {
            d.round(c, rad, hex(0x1b2440));
            if (SDL_Texture* tex = x.icon.empty() ? nullptr : images::get(x.icon)) d.image(tex, c, Fit::Cover, 1, rad);
            else if (x.icon.empty())
                text::draw(d, firstChar(x.title), c.x + tw / 2, c.y + tw / 2, {t.r(2), Weight::Bold, white(), Align::Center, VAlign::Middle});
        }
        ls.color = on ? col::fg : col::dim;
        text::draw(d, text::ellipsize(x.title, ls.font(), tw), c.x, c.y + tw + t.r(.5f), ls);
        pointer::target({c.x, c.y, tw, rh}, [this, i] { hoverResult(i); }, [this, i] { clickResult(i); }, rad);
    }
    d.popClip();
}

void Search::drawHistory(Draw& d, const std::vector<SearchResult>& r) {
    const Theme& t = app().theme;
    Geo g = geo();
    if (r.empty()) return;
    float pad = t.r(.4f), rowh = t.r(4.6f), head = histHeadH();
    bool focused = focus_ == "grid";
    float content = head + r.size() * rowh + 2 * pad;
    if (focused) {
        float top = head + lsel_ * rowh;
        keepVisible(top, top + rowh + 2 * pad, g.gridH, content);
    } else keepVisible(scroll_, scroll_, g.gridH, content);
    pointer::scrollArea({g.gridX, g.gridY, g.gridW, g.gridH}, [this, rowh](float rows) { scroll_ += rows * rowh; });
    d.pushClip({g.gridX - t.r(1), g.gridY, g.gridW + t.r(2), g.gridH});
    float x0 = g.gridX + pad, w = g.gridW - 2 * pad;
    text::draw(d, "Recent searches", x0 + t.r(1.4f), g.gridY + pad - scroll_, {t.r(1.4f), Weight::Regular, white(.6f), Align::Left, VAlign::Top});
    TextStyle st{t.r(1.65f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    SDL_Texture* clock = images::icon("search-history", t.r(1.7f));
    for (int i = 0; i < (int)r.size(); i++) {
        const SearchResult& x = r[(size_t)i];
        Rect c = tileRect(i);
        if (c.y > g.gridY + g.gridH || c.y + c.h < g.gridY) continue;
        if (focused && i == lsel_) {
            d.round(c, t.r(.2f), white(.07f));
            d.outline(c, t.r(.2f), t.r(.1f), white(.85f));
        } else if (!x.clear) d.rect({c.x, c.y + c.h - t.r(.06f), c.w, t.r(.06f)}, white(.08f));
        float tx = c.x + t.r(1.4f), cy = c.y + c.h / 2;
        if (!x.clear) {
            d.icon(clock, tx + t.r(.85f), cy, t.r(1.7f), white(.6f));
            tx += t.r(1.7f) + t.r(1.2f);
        }
        st.color = x.clear ? white(.7f) : white();
        text::draw(d, text::ellipsize(x.title, st.font(), c.x + w - t.r(1.4f) - tx), tx, cy, st);
        pointer::target(c, [this, i] { hoverResult(i); }, [this, i] { clickResult(i); }, t.r(.2f));
    }
    d.popClip();
}

void Search::drawMenu(Draw& d) {
    const Theme& t = app().theme;
    d.rect({0, 0, t.W(), t.H()}, black(.55f));
    pointer::target({0, 0, t.W(), t.H()}, nullptr, [this] { menu_.clear(); });
    float mw = t.r(34.4f), ih = t.r(4.5f), h = t.r(.5f);
    for (auto& m : menu_) h += ih + (m.sep ? t.r(.7f) : 0);
    Rect a = menuAnchor_;
    float x = std::min(t.W() - mw - 16, std::max(16.f, a.x));
    float below = a.y + a.h + 10;
    float y = below + h < t.H() ? below : std::max(16.f, a.y - h - 10);
    d.shadow({x, y, mw, h}, t.r(.3f), t.r(3), black(.6f), 0, t.r(1));
    d.round({x, y, mw, h}, t.r(.3f), rgba(40, 42, 50, .97f));
    pointer::target({x, y, mw, h}, nullptr, nullptr, t.r(.3f));
    float iy = y + t.r(.25f);
    TextStyle st{t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    for (int i = 0; i < (int)menu_.size(); i++) {
        if (menu_[(size_t)i].sep) {
            iy += t.r(.7f);
            d.rect({x + t.r(1.2f), iy - t.r(.4f), mw - t.r(2.4f), t.r(.06f)}, white(.15f));
        }
        Rect c{x, iy, mw, ih};
        if (i == msel_) {
            d.round(c, t.r(.2f), white(.07f));
            d.stroke(c, t.r(.2f), t.r(.1f), white());
        }
        text::draw(d, text::ellipsize(menu_[(size_t)i].label, st.font(), mw - t.r(4.2f)), x + t.r(2.1f), iy + ih / 2, st);
        pointer::target(c, [this, i] { msel_ = i; }, [this, i] {
            msel_ = i;
            menuAction("ok");
        }, t.r(.2f));
        iy += ih;
    }
}

}
