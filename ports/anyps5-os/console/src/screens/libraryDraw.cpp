#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/library.h"
#include "widgets/Widgets.h"

namespace ui {

namespace {
constexpr float kLeft = 10.8f, kTop = 21.f;
const Color kShade = rgba(8, 10, 16);

Color dotColor(const std::string& status) {
    if (status == "Plays") return col::ok;
    if (status == "Stops early") return col::warn;
    return white(.35f);
}
}

void Library::drawBg(Draw& d) {
    const Theme& t = app().theme;
    Rect full{0, 0, t.W(), t.H()};
    d.rect(full, hex(0x0b0d13));
    for (int i = 0; i < 2; i++) {
        float op = artOp_[i].value();
        if (art_[i].empty() || op <= 0) continue;
        if (SDL_Texture* tex = images::get(art_[i])) d.image(tex, full, Fit::Cover, .22f * op);
    }
    d.linear(full, 90, {{0, alpha(kShade, .96f)}, {.45f, alpha(kShade, .82f)}, {1, alpha(kShade, .55f)}});
    d.linear(full, 0, {{0, alpha(kShade, .9f)}, {.4f, alpha(kShade, 0)}, {1, alpha(kShade, 0)}});
}

void Library::drawHead(Draw& d) {
    const Theme& t = app().theme;
    float hy = t.r(5.4f);
    d.icon(images::icon("library", t.r(2.2f)), t.r(4.3f + 1.1f), hy, t.r(2.2f));
    text::draw(d, "Game Library", t.r(kLeft), hy, {t.r(1.95f), Weight::Regular, white(), Align::Left, VAlign::Middle});
    static const char* tabs[] = {"Your Collection", "Installed", "Plays"};
    float x = t.r(kLeft);
    for (int i = 0; i < 3; i++) {
        TextStyle st{t.r(1.95f), Weight::Regular, i == tab_ ? white() : white(.4f), Align::Left, VAlign::Middle};
        float w = text::draw(d, tabs[i], x, t.r(10.2f), st);
        pointer::target({x - t.r(.6f), t.r(10.2f - 1.8f), w + t.r(1.2f), t.r(3.6f)}, nullptr, [this, i] {
            if (tab_ == i) return;
            tab_ = i;
            sel_ = 0;
            rebuild(false);
            moved(false);
        });
        x += w + t.r(3.2f);
    }
    TextStyle ms{t.r(1.4f), Weight::Regular, white(), Align::Center, VAlign::Middle};
    std::string manage = "Manage Collection";
    float mw = text::width(manage, ms.font()) + t.r(3);
    Rect mb{t.W() - t.r(kLeft) - mw, t.r(10.2f) - t.r(1.5f), mw, t.r(3)};
    d.round(mb, mb.h / 2, white(.12f));
    d.stroke(mb, mb.h / 2, std::max(1.f, t.r(.08f)), white(.25f));
    text::draw(d, manage, mb.x + mb.w / 2, mb.y + mb.h / 2, ms);
    pointer::target(mb, nullptr, [] { openCollection("library"); }, mb.h / 2);
    TextStyle small{t.r(1.5f), Weight::Regular, white(.72f), Align::Left, VAlign::Middle};
    text::draw(d, countText(), t.r(kLeft), t.r(18.4f), small);
    static const char* sorts[][2] = {{"recent", "Most Recent"}, {"az", "Alphabetical"}, {"status", "Plays First"}};
    std::string key = app().settings.get("libSort", "recent").get<std::string>(), name = sorts[0][1];
    for (auto& s : sorts)
        if (key == s[0]) name = s[1];
    small.align = Align::Right;
    text::draw(d, "Sort by: " + name, t.W() - t.r(kLeft), t.r(18.4f), small);
    Rect b = sortButton();
    d.circle(b.x + b.w / 2, b.y + b.h / 2, b.w / 2, white(.12f));
    d.icon(images::icon("sort", t.r(1.6f)), b.x + b.w / 2, b.y + b.h / 2, t.r(1.6f));
    pointer::target(b, nullptr, [] { psmenu::show(psmenu::sortItems(), sortButton(), false); }, b.w / 2);
}

void Library::drawTile(Draw& d, const LibEntry& e, Rect r, bool on) {
    const Theme& t = app().theme;
    float rad = t.r(.9f);
    using collection::State;
    if (on) widgets::focusGlow(d, r, rad);
    d.round(r, rad, hex(0x1c2233));
    float artA = e.st == State::NotInstalled || e.st == State::Unsupported ? .5f : 1.f;
    SDL_Texture* tex = e.g.icon.empty() ? nullptr : images::get(e.g.icon);
    if (tex) d.image(tex, r, Fit::Cover, artA, rad);
    else if (e.g.icon.empty())
        text::draw(d, widgets::initial(json{{"name", e.g.title}}), r.x + r.w / 2, r.y + r.h / 2, {t.r(3.4f), Weight::Bold, white(artA), Align::Center, VAlign::Middle});
    if (e.inLib) {
        float dr = t.r(.35f), cx = r.x + r.w - t.r(.6f) - dr, cy = r.y + t.r(.6f) + dr;
        d.circle(cx, cy, dr, black(.6f));
        d.circle(cx, cy, dr - t.r(.12f), dotColor(e.g.status));
    }
    std::string lab = collection::supported() ? collection::labelFor(e.g.id) : "";
    if (lab.empty() && collection::supported() && collection::loaded() && e.inLib && !e.g.exe.empty()) lab = "Installed";
    if (!lab.empty()) {
        TextStyle b{t.r(1.05f), Weight::Medium, white(), Align::Left, VAlign::Middle};
        bool dl = (e.st == State::NotInstalled && lab != "No game data") || e.st == State::Installing || e.st == State::Failed;
        float ic = dl ? t.r(1.2f) + t.r(.35f) : 0;
        float bw = text::width(lab, b.font()) + t.r(1.1f) + ic, bh = t.r(2.1f);
        Rect br{r.x + t.r(.6f), r.y + t.r(.6f), bw, bh};
        d.round(br, t.r(.35f), black(.7f));
        if (dl) d.icon(images::icon("download", t.r(1.2f)), br.x + t.r(.55f) + t.r(.6f), br.y + bh / 2, t.r(1.2f));
        text::draw(d, lab, br.x + t.r(.55f) + ic, br.y + bh / 2, b);
    }
    float lift = e.st == State::Installing ? t.r(1.1f) : 0;
    float op = on ? nameOp_.value() : 0;
    if (op > 0) {
        TextStyle nm{t.r(1.5f), Weight::Regular, white(), Align::Left, VAlign::Top};
        float lh = text::lineHeight(nm.font());
        float h = t.r(2.6f + .8f) + lh + lift;
        d.pushAlpha(op);
        d.pushClip(r);
        d.linear({r.x, r.y + r.h - h, r.w, h}, 0, {{0, black(.85f)}, {1, black(0)}});
        d.popClip();
        text::draw(d, text::ellipsize(e.g.title, nm.font(), r.w - t.r(2)), r.x + t.r(1), r.y + r.h - t.r(.8f) - lift - lh, nm);
        d.popAlpha();
    }
    if (e.st == State::Installing) {
        Rect bar{r.x + t.r(1), r.y + r.h - t.r(1.1f), r.w - t.r(2), t.r(.35f)};
        d.round(bar, bar.h / 2, black(.55f));
        d.round(bar, bar.h / 2, white(.22f));
        if (e.pct > 0) d.round({bar.x, bar.y, bar.w * e.pct / 100.f, bar.h}, bar.h / 2, white());
    }
}

void Library::draw(Draw& d) {
    const Theme& t = app().theme;
    drawBg(d);
    drawHead(d);
    Rect view{t.r(kLeft), t.r(kTop), t.W() - t.r(kLeft), t.H() - t.r(kTop)};
    if (list_.empty()) {
        text::draw(d, "Nothing here", t.r(kLeft + .3f), t.r(kTop + .3f), {t.r(1.4f), Weight::Regular, col::dim});
        return;
    }
    pointer::scrollArea(view, [this](float rows) { scrollRows(rows); });
    d.pushClip(view);
    float sc = scroll_.value();
    for (int i = 0; i < (int)list_.size(); i++) {
        Rect r = tileRect(i, sc);
        if (r.y > t.H() || r.y + r.h < view.y - t.r(2)) continue;
        if (i != sel_) drawTile(d, list_[i], r, false);
        pointer::target(r, [this, i] {
            if (sel_ == i) return;
            sel_ = i;
            moved(false);
        }, [this, i] {
            sel_ = i;
            activate();
        }, t.r(.9f));
    }
    if (const LibEntry* e = cur()) drawTile(d, *e, tileRect(sel_, sc), true);
    d.popClip();
}

}
