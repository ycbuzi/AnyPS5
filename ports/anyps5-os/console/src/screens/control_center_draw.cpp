#include "core/Glyphs.h"
#include "screens/control_center_screen.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::cc {

void ControlCenter::layout(size_t n) {
    const Theme& t = app().theme;
    bool fresh = w_.size() != n;
    w_.resize(n);
    h_.resize(n);
    float x = t.r(5.1f), selRight = 0;
    for (size_t i = 0; i < n; i++) {
        bool sel = crow_ == 0 && (int)i == card_;
        float tw = t.r(sel ? 27.25f : 22.5f), th = t.r(sel ? 32.9f : 25);
        if (fresh) {
            w_[i].snap(tw);
            h_[i].snap(th);
        } else {
            w_[i].set(tw, .2f, easeCss);
            h_[i].set(th, .2f, easeCss);
        }
        x += tw;
        if ((int)i == card_) selRight = x;
        x += t.r(.9f);
    }
    float target = std::max(0.f, selRight - (t.W() - t.r(5.1f)));
    if (fresh) scroll_.snap(target);
    else if (target != scrollTarget_) scroll_.set(target, .25f, easeCss);
    scrollTarget_ = target;
}

void ControlCenter::draw(Draw& d) {
    const Theme& t = app().theme;
    drawIngame(d);
    pointer::target({0, 0, t.W(), t.H()}, nullptr, [] { app().act({"back", ""}); });
    pointer::target({0, t.H() - t.r(36), t.W(), t.r(36)}, nullptr, nullptr);
    d.linear({0, t.H() - t.r(36), t.W(), t.r(36)}, 0, {{0, black(.94f)}, {.42f, black(.78f)}, {1, black(0)}});
    if (app().ingame)
        text::draw(d, app().clockText(), t.W() - t.r(4.4f), t.r(3.9f), {t.r(2.1f), Weight::Light, white(), Align::Right, VAlign::Middle, t.r(.8f)});
    auto cs = cards();
    auto is = icons();
    if (card_ >= (int)cs.size()) card_ = (int)cs.size() - 1;
    layout(cs.size());
    float r = rise_.value();
    drawCards(d, cs, cardsA_.value() * r, t.r(3.2f) * (1 - r));
    drawIcons(d, is, t.r(2.4f) * (1 - r));
    panels_.draw(d);
}

void ControlCenter::drawCards(Draw& d, const std::vector<Card>& cs, float alpha, float dy) {
    const Theme& t = app().theme;
    if (alpha <= 0) return;
    d.pushAlpha(alpha);
    float x = t.r(5.1f) - scroll_.value(), bottom = t.H() - t.r(11.9f) + dy;
    for (size_t i = 0; i < cs.size(); i++) {
        float w = w_[i].value(), h = h_[i].value();
        Rect rc{x, bottom - h, w, h};
        bool sel = crow_ == 0 && (int)i == card_;
        if (rc.x < t.W() && rc.x + rc.w > 0) {
            drawCard(d, cs[i], rc, sel);
            if (!panels_.on()) pointer::target(rc, [this, i] { hoverCard((int)i); }, [this, i] { hoverCard((int)i); app().act({"ok", ""}); }, t.r(1.2f));
        }
        if (sel) drawHint(d, cs[i], rc.x + rc.w / 2);
        x += w + t.r(.9f);
    }
    d.popAlpha();
}

void ControlCenter::drawCard(Draw& d, const Card& c, Rect r, bool sel) {
    const Theme& t = app().theme;
    float rad = t.r(1.2f);
    d.round(r, rad, hex(0x1b2133));
    SDL_Texture* img = c.img.empty() ? nullptr : images::get(c.img);
    if (img) d.image(img, r, Fit::Cover, 1, rad);
    d.linear({r.x, r.y + rad, r.w, r.h - 2 * rad}, 0, {{0, black(.79f)}, {.55f, black(.15f)}, {1, black(.24f)}});
    d.pushClip({r.x, r.y, r.w, rad});
    d.round(r, rad, black(.25f));
    d.popClip();
    d.pushClip({r.x, r.y + r.h - rad, r.w, rad});
    d.round(r, rad, black(.8f));
    d.popClip();
    if (sel) d.outline(r, rad, t.r(.13f), white());
    float gs = t.r(2.4f);
    Rect gr{r.x + t.r(1.1f), r.y + t.r(1.1f), gs, gs};
    if (!c.icon.empty()) drawThumb(d, gr, t.r(.4f), c.icon, hex(0x333333));
    else {
        d.round(gr, t.r(.4f), hex(0x14182a));
        if (SDL_Texture* logo = images::asset("logo-mark.png")) d.image(logo, inset(gr, gs * .19f), Fit::Contain);
    }
    if (!img && !c.glyph.empty() && c.big.empty()) {
        float s = t.r(sel ? 7.f : 5.6f);
        if (SDL_Texture* g = images::icon(c.glyph, s)) d.icon(g, r.x + r.w / 2, r.y + r.h * .34f, s, white(.85f));
    }
    float x = r.x + t.r(1.4f), w = r.w - t.r(2.8f), y = r.y + r.h - t.r(1.2f);
    Font fs{t.r(1.35f), Weight::Regular}, fb{t.r(1.85f), Weight::Semibold}, fl{t.r(1.4f), Weight::Regular};
    auto sub = c.sub.empty() ? std::vector<std::string>{} : text::wrap(c.sub, fs, w, 2);
    float sh = text::lineHeight(fs);
    y -= sh * sub.size();
    text::drawLines(d, sub, x, y, sh, {fs.size, fs.weight, white(.72f), Align::Left, VAlign::Top});
    y -= t.r(.2f);
    auto title = text::wrap(c.title, fb, w, 2);
    float th = fb.size * 1.15f;
    y -= th * title.size();
    float off = (th - text::lineHeight(fb)) / 2;
    text::drawLines(d, title, x, y + off, th, {fb.size, fb.weight, white(), Align::Left, VAlign::Top});
    y -= text::lineHeight(fl);
    text::draw(d, text::ellipsize(c.label, fl, w), x, y, {fl.size, fl.weight, white(.72f), Align::Left, VAlign::Top});
    if (!c.badge.empty()) {
        Font fg{t.r(1.25f), Weight::Regular};
        float bh = text::lineHeight(fg) + t.r(.5f), bw = text::width(c.badge, fg) + t.r(1.4f);
        y -= t.r(1.4f) + bh;
        Rect b{x, y, std::min(bw, w), bh};
        d.stroke(b, 0, t.r(.1f), white());
        text::draw(d, c.badge, b.x + t.r(.7f), b.y + bh / 2, {fg.size, fg.weight, white(), Align::Left, VAlign::Middle});
        y -= t.r(.6f);
    }
    if (!c.big.empty()) {
        Font fg{t.r(3.75f), Weight::Bold};
        y -= fg.size;
        text::draw(d, text::ellipsize(c.big, fg, w), x, y + fg.size / 2, {fg.size, fg.weight, white(), Align::Left, VAlign::Middle});
    }
}

void ControlCenter::drawHint(Draw& d, const Card& c, float cx) {
    const Theme& t = app().theme;
    Font fl{t.r(1.4f), Weight::Regular};
    std::string label = c.hint.empty() ? "Select" : c.hint;
    std::string templ = glyph::has("ok") ? "{ok} " + label : label;
    glyph::line(d, templ, cx, t.H() - t.r(8.9f) - t.r(1), {fl.size, fl.weight, white(), Align::Center, VAlign::Middle});
}

void ControlCenter::drawIcons(Draw& d, const std::vector<CcIcon>& is, float dy) {
    const Theme& t = app().theme;
    float cy = iconY() + dy, gs = t.r(2.1f);
    for (int i = 0; i < (int)is.size() && i < kIcons; i++) {
        const CcIcon& c = is[i];
        float cx = iconX(i);
        bool sel = crow_ == 1 && i == qsel_;
        float hr = t.r(2.6f);
        if (!panels_.on()) pointer::target({cx - hr, cy - hr, 2 * hr, 2 * hr}, [this, i] { hoverIcon(i); }, [this, i] { hoverIcon(i); app().act({"ok", ""}); }, hr);
        if (sel) {
            float rr = t.r(2.2f);
            Rect ring{cx - rr, cy - rr, 2 * rr, 2 * rr};
            d.soft(ring, rr, t.r(.7f), white(.3f));
            d.circle(cx, cy, rr, white(.16f));
            d.outline(ring, rr, t.r(.13f), white(.9f));
            if (!panels_.on()) {
                Font fl{t.r(1.45f), Weight::Regular};
                text::draw(d, c.label, cx, cy - t.r(1.1f) - t.r(1.6f) - text::lineHeight(fl), {fl.size, fl.weight, white(), Align::Center, VAlign::Top});
            }
        }
        Playing pg;
        if (c.kind == IconKind::Switcher && playing(pg)) {
            float s = t.r(2.9f);
            drawThumb(d, {cx - s / 2, cy - s / 2, s, s}, t.r(.5f), pg.icon, hex(0x333333));
        } else if (c.kind == IconKind::Avatar) {
            float s = t.r(2.3f);
            if (app().me.is_object()) widgets::avatar(d, cx, cy, s, app().me, t.r(1));
            else {
                d.circle(cx, cy, s / 2, hex(0x555555));
                text::draw(d, "?", cx, cy, {t.r(1), Weight::Semibold, white(), Align::Center, VAlign::Middle});
            }
            float dc = t.r(.83f), dr = t.r(.35f);
            d.circle(cx + dc, cy + dc, dr + t.r(.12f), black());
            d.circle(cx + dc, cy + dc, dr, hex(0x2fd36b));
        } else if (SDL_Texture* g = images::icon(c.svg, gs)) {
            d.icon(g, cx, cy, gs);
        }
        if (c.act) d.circle(cx, cy + t.r(1.9f), t.r(.2f), hex(0x2fd36b));
    }
}

}
