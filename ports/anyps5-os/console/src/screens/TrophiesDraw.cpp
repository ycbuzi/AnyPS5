#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/Trophies.h"

namespace ui {

using trophies::Set;
using trophies::Trophy;

namespace {

bool narrow() {
    const Theme& t = app().theme;
    return t.W() * 3 <= t.H() * 4;
}

TextStyle style(float size, Weight w, Color c, Align a = Align::Left, VAlign v = VAlign::Middle) { return {size, w, c, a, v}; }

void bar(Draw& d, Rect r, int pct) {
    const Theme& t = app().theme;
    d.round(r, t.r(.2f), white(.22f));
    if (pct > 0) {
        d.pushClip({r.x, r.y, r.w * std::min(pct, 100) / 100.f, r.h});
        d.round(r, t.r(.2f), white());
        d.popClip();
    }
}

void iconBox(Draw& d, Rect r, const std::string& url, float radius) {
    d.round(r, radius, hex(0x2a2d38));
    if (!url.empty()) d.image(images::get(url), r, Fit::Cover, 1.f, radius);
}

}

Rect Trophies::listRect() const {
    const Theme& t = app().theme;
    if (narrow()) {
        float top = t.r(view_ == "list" ? 18 : 15);
        return {t.r(3), top, t.W() - t.r(6), t.H() - t.r(4.6f) - top};
    }
    float top = t.r(view_ == "list" ? 14.2f : 11.4f);
    return {t.r(10.6f), top, t.W() - t.r(10.6f) - t.r(9.9f), t.H() - t.r(4.6f) - top};
}

float Trophies::sumItem(Draw& d, float x, float cy, const std::string& kind, const std::string& a, const std::string& b, int pct, bool measure) {
    const Theme& t = app().theme;
    if (kind == "lv") {
        TextStyle st = style(t.r(2.4f), Weight::Light, white());
        float w = t.r(3.4f) + t.r(.8f) + text::width(a, st.font());
        if (measure) return w;
        float s = t.r(3.4f);
        d.icon(images::icon("trlevel", s), x + s / 2, cy, s, hex(trophies::levelColor(pct)));
        d.icon(images::icon("trlevel-edge", s), x + s / 2, cy, s, white());
        text::draw(d, a, x + s + t.r(.8f), cy, st);
        return w;
    }
    if (kind == "tx") {
        TextStyle sm = style(t.r(1.25f), Weight::Regular, white(.6f), Align::Left, VAlign::Top);
        TextStyle bs = style(t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Top);
        float w = std::max(text::width(a, sm.font()), text::width(b, bs.font()));
        if (measure) return w;
        float h1 = text::lineHeight(sm.font()), h2 = text::lineHeight(bs.font()), hb = pct >= 0 ? t.r(.6f) : 0;
        float y = cy - (h1 + h2 + hb) / 2;
        text::draw(d, a, x, y, sm);
        text::draw(d, b, x, y + h1, bs);
        if (pct >= 0) bar(d, {x, y + h1 + h2 + t.r(.35f), w, t.r(.25f)}, pct);
        return w;
    }
    TextStyle st = style(t.r(1.5f), Weight::Regular, white());
    float s = t.r(2.6f);
    float w = s + (b.empty() ? 0 : t.r(.6f) + text::width(b, st.font()));
    if (measure) return w;
    kit::cup(d, a, x + s / 2, cy, s);
    if (!b.empty()) text::draw(d, b, x + s + t.r(.6f), cy, st);
    return w;
}

void Trophies::draw(Draw& d) {
    const Theme& t = app().theme;
    kit::setbg(d);
    trophies::store().applyUser();
    if (view_ == "list" && !trophies::store().byKey(key_)) {
        if (trophies::store().loaded()) view_ = "games";
    }
    if (view_ == "games") drawGames(d);
    else drawList(d);
    menu_.draw(d);
    (void)t;
}

void Trophies::drawGames(Draw& d) {
    const Theme& t = app().theme;
    auto& store = trophies::store();
    float cy = t.r(6.4f);
    TextStyle head = style(t.r(2.85f), Weight::Light, white());
    float hx = narrow() ? t.r(3) : t.r(5.9f);
    float hw = text::draw(d, "Trophies", hx, cy, head);
    float hh = text::lineHeight(head.font());
    headRect_ = {hx, cy - hh / 2, hw, hh};

    int pts = 0, total = 0;
    std::array<int, 4> got{};
    for (auto& s : store.games()) {
        pts += s.st.pts;
        total += s.st.earned;
        for (size_t g = 0; g < 4; g++) got[g] += s.st.got[g];
    }
    trophies::Level lv = trophies::level(pts);
    struct Item {
        std::string kind, a, b;
        int pct;
    };
    std::vector<Item> items = {{"lv", std::to_string(lv.level), "", lv.level}, {"tx", "Total", std::to_string(total), -1}};
    for (size_t g = 0; g < 4; g++) items.push_back({"g", trophies::kGrades[g], std::to_string(got[g]), 0});
    float gap = t.r(narrow() ? 1.8f : 2.4f), tw = 0, sy = narrow() ? t.r(11.6f) : cy;
    for (auto& it : items) tw += sumItem(d, 0, sy, it.kind, it.a, it.b, it.pct, true);
    tw += gap * (items.size() - 1);
    float x = narrow() ? t.r(3) : t.W() - t.r(9.9f) - tw;
    for (auto& it : items) x += sumItem(d, x, sy, it.kind, it.a, it.b, it.pct, false) + gap;

    Rect lr = listRect();
    auto sets = store.sets(trophySetting("trSort", "recent"));
    if (sel_ >= (int)sets.size()) sel_ = std::max(0, (int)sets.size() - 1);
    TextStyle em = style(t.r(1.6f), Weight::Regular, white(.65f), Align::Left, VAlign::Top);
    if (!store.loaded() || sets.empty()) {
        std::string msg = !store.loaded() ? "Trophy data is not ready yet. It is read from every game's files every minute." : "No trophies yet. Play a game and the trophies you earn show up here.";
        auto lines = text::wrap(msg, em.font(), lr.w - t.r(.4f) - t.r(2.4f));
        text::drawLines(d, lines, lr.x + t.r(.2f) + t.r(1.2f), lr.y + t.r(.2f) + t.r(2), t.r(1.6f) * 1.5f, em);
        return;
    }
    float pitch = t.r(8.9f), pad = t.r(.2f);
    float top = pointer::active() ? scroll_ : pad + sel_ * pitch;
    scroll_ = kit::scrollNearest(scroll_, top, pointer::active() ? top : top + pitch, lr.h, pad * 2 + sets.size() * pitch);
    pointer::scrollArea(lr, [this, pitch](float rows) { scroll_ += rows * pitch; });
    d.pushClip(lr);
    for (size_t i = 0; i < sets.size(); i++) {
        float y = lr.y + pad + i * pitch - scroll_;
        if (y > lr.y + lr.h || y + pitch < lr.y) continue;
        drawGameRow(d, *sets[i], {lr.x + pad, y, lr.w - 2 * pad, pitch}, (int)i == sel_);
        int n = (int)i;
        pointer::target({lr.x + pad, y, lr.w - 2 * pad, pitch}, [this, n] { sel_ = n; }, [this, n] {
            sel_ = n;
            action({"ok", ""});
        }, t.r(.3f));
    }
    d.popClip();
}

void Trophies::drawGameRow(Draw& d, const Set& s, Rect r, bool sel) {
    const Theme& t = app().theme;
    float rad = t.r(.3f), cy = r.y + r.h / 2;
    if (sel) {
        kit::glow(d, r, rad);
        d.round(r, rad, white(.08f));
    }
    d.rect({r.x, r.y + r.h - t.r(.06f), r.w, t.r(.06f)}, white(.09f));
    float x = r.x + t.r(1.2f), right = r.x + r.w - t.r(1.2f), g = t.r(1.6f);
    float ic = t.r(6.2f);
    iconBox(d, {x, cy - ic / 2, ic, ic}, s.icon, rad);

    TextStyle ct = style(t.r(1.5f), Weight::Regular, white());
    float cupS = t.r(2.2f), ctGap = t.r(narrow() ? 1.2f : 2.2f), cx = right;
    for (int gi = 3; gi >= 1; gi--) {
        std::string n = std::to_string(s.st.got[(size_t)gi]);
        float nw = text::width(n, ct.font());
        text::draw(d, n, cx - nw, cy, ct);
        kit::cup(d, trophies::kGrades[(size_t)gi], cx - nw - t.r(.5f) - cupS / 2, cy, cupS);
        cx -= nw + t.r(.5f) + cupS + ctGap;
    }
    float ctW = narrow() ? right - cx - ctGap : t.r(19);
    float pcR = right - ctW - g, pcW = t.r(narrow() ? 6.5f : 8);
    TextStyle pc = style(t.r(2), Weight::Light, white(), Align::Center, VAlign::Top);
    float ph = text::lineHeight(pc.font()), blk = ph + t.r(.6f), py = cy - blk / 2;
    text::draw(d, std::to_string(s.st.pct) + "%", pcR - pcW / 2, py, pc);
    bar(d, {pcR - pcW, py + ph + t.r(.35f), pcW, t.r(.25f)}, s.st.pct);
    float plR = pcR - pcW - g, plW = t.r(2.8f);
    if (s.st.plat) kit::cup(d, "platinum", plR - plW + t.r(1.3f), cy, t.r(2.6f));

    float nx = x + ic + g, nw = plR - plW - g - nx;
    TextStyle nm = style(t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Top);
    TextStyle bd = style(t.r(1.25f), Weight::Regular, white(.8f), Align::Left, VAlign::Top);
    float h1 = text::lineHeight(nm.font()), h2 = text::lineHeight(bd.font()) + t.r(.16f);
    float ny = cy - (h1 + t.r(.5f) + h2) / 2;
    text::draw(d, text::ellipsize(s.title, nm.font(), nw), nx, ny, nm);
    std::string plat = s.id.rfind("CUSA", 0) == 0 ? "PS4" : "PS5";
    Rect b{nx, ny + h1 + t.r(.5f), text::width(plat, bd.font()) + t.r(.8f) + t.r(.16f), h2};
    d.stroke(b, t.r(.15f), t.r(.08f), white(.5f));
    text::draw(d, plat, b.x + t.r(.48f), b.y + t.r(.08f), bd);
}

void Trophies::drawList(Draw& d) {
    const Theme& t = app().theme;
    auto& store = trophies::store();
    const Set* s = store.byKey(key_);
    if (!s) return;
    const trophies::Stats& st = s->st;
    float cy = t.r(6.4f);
    TextStyle head = style(t.r(2.85f), Weight::Light, white());
    float ic = t.r(3.4f), hx = narrow() ? t.r(3) : t.r(5.9f);
    iconBox(d, {hx, cy - ic / 2, ic, ic}, s->icon, t.r(.3f));
    std::string title = text::ellipsize(s->title, head.font(), t.r(narrow() ? 62 : 56) - ic - t.r(1.2f));
    float hw = text::draw(d, title, hx + ic + t.r(1.2f), cy, head);
    float hh = std::max(ic, text::lineHeight(head.font()));
    headRect_ = {hx, cy - hh / 2, ic + t.r(1.2f) + hw, hh};

    struct Item {
        std::string kind, a, b;
        int pct;
    };
    std::vector<Item> items;
    if (st.hasPlat) items.push_back({"g", st.plat ? "platinum" : "none", "", 0});
    items.push_back({"tx", "Progress", std::to_string(st.pct) + "%", st.pct});
    items.push_back({"tx", "Earned", std::to_string(st.earned) + "/" + std::to_string(st.total), -1});
    for (size_t g = 1; g < 4; g++) items.push_back({"g", trophies::kGrades[g], std::to_string(st.got[g]), 0});
    float gap = t.r(narrow() ? 1.8f : 2.4f), tw = 0, sy = narrow() ? t.r(11.6f) : cy;
    for (auto& it : items) tw += sumItem(d, 0, sy, it.kind, it.a, it.b, it.pct, true);
    tw += gap * (items.size() - 1);
    float x = narrow() ? t.r(3) : t.W() - t.r(9.9f) - tw;
    for (auto& it : items) x += sumItem(d, x, sy, it.kind, it.a, it.b, it.pct, false) + gap;

    TextStyle bs = style(t.r(1.3f), Weight::Regular, white(.65f));
    float bl = narrow() ? t.r(3) : t.r(10.6f), br = t.W() - (narrow() ? t.r(3) : t.r(9.9f)), by = t.r(narrow() ? 16.4f : 12.6f), bh = text::lineHeight(bs.font());
    barRect_ = {bl, by - bh / 2, br - bl, bh};
    text::draw(d, "All trophies: " + std::to_string(st.total), bl, by, bs);
    TextStyle bsr = bs;
    bsr.align = Align::Right;
    std::string sortLabel = "Sort by: " + trophySortName(trophySetting("trTSort", "default"));
    text::draw(d, sortLabel, br, by, bsr);
    float sw = text::width(sortLabel, bsr.font());
    pointer::target({br - sw - t.r(.6f), by - bh / 2 - t.r(.4f), sw + t.r(1.2f), bh + t.r(.8f)}, nullptr, [this] { sortMenu(); }, t.r(.2f));

    auto list = store.list(*s, trophySetting("trTSort", "default"));
    if (sel_ >= (int)list.size()) sel_ = std::max(0, (int)list.size() - 1);
    Rect lr = listRect();
    float h = t.r(10.8f), pitch = h + t.r(1.35f), pad = t.r(.2f);
    float top = pointer::active() ? scroll_ : pad + sel_ * pitch;
    scroll_ = kit::scrollNearest(scroll_, top, pointer::active() ? top : top + h, lr.h, pad * 2 + list.size() * pitch);
    pointer::scrollArea(lr, [this, pitch](float rows) { scroll_ += rows * pitch; });
    d.pushClip(lr);
    for (size_t i = 0; i < list.size(); i++) {
        float y = lr.y + pad + i * pitch - scroll_;
        if (y > lr.y + lr.h || y + h < lr.y) continue;
        drawCard(d, *s, *list[i], {lr.x + pad, y, lr.w - 2 * pad, h}, (int)i == sel_);
        int n = (int)i;
        pointer::target({lr.x + pad, y, lr.w - 2 * pad, h}, [this, n] { sel_ = n; }, [this, n] { sel_ = n; }, t.r(.3f));
    }
    d.popClip();
}

void Trophies::drawCard(Draw& d, const Set& s, const Trophy& tr, Rect r, bool sel) {
    const Theme& t = app().theme;
    float rad = t.r(.4f);
    if (sel) kit::glow(d, r, rad);
    d.round(r, rad, white(sel ? .08f : .06f));
    bool hide = tr.hidden && !tr.earned, lock = !tr.earned;
    float x0 = r.x + t.r(2), top = r.y + t.r(1.6f), cw = r.w - t.r(4);
    float ts = t.r(7.6f), ir = t.r(.3f);
    Rect ib{x0, top, ts, ts};
    d.round(ib, ir, lock ? hex(0x15161c) : hex(0x2a2d38));
    if (!hide) d.image(lock ? kit::dimGray(d, tr.icon, .5f) : images::get(tr.icon), ib, Fit::Cover, 1.f, ir);
    if (hide) text::draw(d, "?", ib.x + ts / 2, ib.y + ts / 2, style(t.r(3), Weight::Regular, white(.25f), Align::Center));

    bool nar = narrow();
    float x1 = x0 + ts + t.r(2), tmW = cw * (nar ? .4f : .34f);
    TextStyle nm = style(t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Top);
    TextStyle sp = style(t.r(1.3f), Weight::Regular, white(.65f));
    float h1 = text::lineHeight(nm.font()), hs = std::max(text::lineHeight(sp.font()), t.r(1.6f)), g = t.r(.45f);
    text::draw(d, text::ellipsize(hide ? std::string("Hidden Trophy") : tr.name, nm.font(), tmW), x1, top, nm);
    float sy = top + h1 + g + hs / 2;
    kit::cup(d, tr.grade, x1 + t.r(.8f), sy, t.r(1.6f));
    std::string grp;
    if (tr.group != -1 && s.groups.is_object()) {
        auto it = s.groups.find(std::to_string(tr.group));
        if (it != s.groups.end() && it->is_string() && !it->get<std::string>().empty()) grp = " \xC2\xB7 " + it->get<std::string>();
    }
    float gx = x1 + t.r(1.6f) + t.r(.5f);
    d.pushClip({x1, sy - hs / 2, tmW, hs});
    text::draw(d, trophies::gradeName(tr.grade) + grp, gx, sy, sp);
    d.popClip();
    text::draw(d, tr.earned ? "Earned" : "Not earned yet", x1, sy + hs / 2 + g + hs / 2, sp);
    if (nar && tr.earned) text::draw(d, trophies::when(tr.earned), x1, sy + 2 * (hs + g), sp);

    float x2 = x1 + tmW + t.r(2), dtW = nar ? -t.r(2) : t.r(13);
    if (tr.earned && !nar) text::draw(d, trophies::when(tr.earned), x2 + dtW, top + t.r(.3f), style(t.r(1.3f), Weight::Regular, white(.7f), Align::Right, VAlign::Top));

    float x3 = x2 + dtW + t.r(2), ch = r.h - t.r(3.2f);
    d.rect({x3, top, t.r(.06f), ch}, white(.15f));
    TextStyle ds = style(t.r(1.4f), Weight::Regular, white(.78f), Align::Left, VAlign::Top);
    float tx = x3 + t.r(.06f) + t.r(1.6f), tw = x0 + cw - tx;
    std::string desc = hide ? std::string("Keep playing to find out what this trophy is.") : tr.detail;
    if (tw > 0) text::drawLines(d, text::wrap(desc, ds.font(), tw, 4), tx, top, t.r(1.4f) * 1.35f, ds);
}

}
