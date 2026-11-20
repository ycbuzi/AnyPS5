#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/Home.h"
#include "widgets/Widgets.h"

namespace ui {

static constexpr float kSmall = 6.5f, kBig = 10.94f, kGap = .65f, kRowLeft = 10.5f;

void Home::drawArt(Draw& d) {
    const Theme& t = app().theme;
    Rect full{0, 0, t.W(), t.H()};
    d.rect(full, col::page);
    for (int i : {1 - artOn_, artOn_}) {
        ArtLayer& L = art_[i];
        float op = L.op.value();
        if (op <= 0 || L.src.empty()) continue;
        SDL_Texture* tex = images::get(L.src);
        if (!tex) continue;
        float z = L.zoom.value();
        Rect r{t.W() * (1 - z) / 2, t.H() * (1 - z) / 2, t.W() * z, t.H() * z};
        d.image(tex, r, Fit::Cover, op);
    }
    d.linear(full, 90, {{0, black(.5f)}, {.42f, black(.12f)}, {.65f, black(0)}, {1, black(0)}});
    d.linear(full, 0, {{0, black(.7f)}, {.38f, black(0)}, {1, black(0)}});
    d.linear(full, 180, {{0, black(.45f)}, {.24f, black(0)}, {1, black(0)}});
}

void Home::drawTop(Draw& d) {
    App& A = app();
    const Theme& t = A.theme;
    float cy = t.r(3.875f);
    float x = t.r(5.7f);
    for (const char* tb : {"Games", "Media"}) {
        bool on = (tab_ == "games") == (std::string(tb) == "Games");
        TextStyle st{t.r(2.1f), on ? Weight::Semibold : Weight::Regular, on ? white() : white(.55f), Align::Left, VAlign::Middle};
        float tw = text::draw(d, tb, x, cy, st);
        std::string to = std::string(tb) == "Games" ? "games" : "media";
        pointer::target({x - t.r(1.2f), cy - t.r(2), tw + t.r(2.4f), t.r(4)}, nullptr, [this, to] { setTab(to); }, t.r(.6f));
        x += tw + t.r(4.6f);
    }
    float right = t.W() - t.r(5.25f);
    TextStyle clk{t.r(2.1f), Weight::Light, white(), Align::Right, VAlign::Middle};
    float cw = std::max(t.r(8.6f), text::width(A.clockText(), clk.font()));
    text::draw(d, A.clockText(), right, cy, clk);
    right -= cw + t.r(3.4f);
    float me = t.r(2.5f);
    float mcx = right - me / 2;
    if (A.me.is_object()) widgets::avatar(d, mcx, cy, me, A.me, t.r(1.1f));
    float dotR = t.r(.4f);
    float dcx = right + t.r(.15f) - dotR, dcy = cy + me / 2 + t.r(.15f) - dotR;
    d.circle(dcx, dcy, dotR, black());
    d.circle(dcx, dcy, dotR - t.r(.15f), hex(0x2fd36b));
    right -= me + t.r(3.4f);
    for (const char* ic : {"help", "menu", "settings", "search"}) {
        float box = t.r(2.8f);
        d.icon(images::icon(ic, t.r(2.1f)), right - box / 2, cy, t.r(2.1f));
        std::string to = std::string(ic) == "search" ? "search" : std::string(ic) == "settings" ? "settings" : "";
        if (!to.empty()) pointer::target({right - box, cy - box / 2, box, box}, nullptr, [to] { app().open(to); }, box / 2);
        right -= box + t.r(3.4f);
    }
}

void Home::drawRow(Draw& d) {
    const Theme& t = app().theme;
    bool media = tab_ == "media";
    float x = t.r(kRowLeft) - shift_.value(), y = t.r(7.8f);
    pointer::scrollArea({0, y, t.W(), t.r(kBig)}, [this](float rows) { stepRow(rows); });
    for (size_t i = 0; i < items_.size(); i++) {
        const HomeItem& it = items_[i];
        float k = size_[i].value();
        float s = t.r(kSmall + (kBig - kSmall) * k);
        float rad = t.r(.9f + (1.4f - .9f) * k);
        Rect r{x, y, s, s};
        x += s + t.r(kGap);
        if (r.x > t.W() || r.x + r.w < 0) continue;
        pointer::target(r, [this, i] { pickRow((int)i); }, [this, i] { pickRow((int)i); activate(); }, rad);
        bool foc = (int)i == sel_ && focus_ == "row";
        if (foc) {
            d.shadow(r, rad, t.r(2), black(.55f), 0, t.r(.6f));
            d.soft(r, rad, t.r(media ? 1.8f : 1.6f) * .5f, media ? rgba(190, 170, 255, .45f) : white(.25f));
        }
        if (media) {
            d.round(r, rad, hex(0x1f2131));
            float is = t.r(2.6f + 1.0f * k);
            d.icon(images::icon(it.media, is), r.x + s / 2, r.y + s / 2, is);
        } else if (it.lib) {
            d.round(r, rad, rgba(40, 46, 64, .85f));
            d.icon(images::icon(it.game.raw.is_object() ? it.game.raw.value("icon", "library") : "library", t.r(2.4f)), r.x + s / 2, r.y + s / 2, t.r(2.4f));
        } else {
            d.round(r, rad, hex(0x1c2233));
            SDL_Texture* tex = it.game.icon.empty() ? nullptr : images::get(it.game.icon);
            if (tex) d.image(tex, r, Fit::Cover, 1, rad);
            else text::draw(d, widgets::initial(json{{"name", it.title}}), r.x + s / 2, r.y + s / 2, {t.r(2), Weight::Bold, white(), Align::Center, VAlign::Middle});
            if (it.running) {
                TextStyle b{t.r(.95f), Weight::Regular, white(), Align::Left, VAlign::Middle};
                float bw = text::width("Playing", b.font()) + t.r(.9f), bh = text::lineHeight(b.font()) + t.r(.3f);
                Rect br{r.x + t.r(.4f), r.y + t.r(.4f), bw, bh};
                d.round(br, t.r(.3f), black(.7f));
                text::draw(d, "Playing", br.x + t.r(.45f), br.y + bh / 2, b);
            }
        }
        if (foc) d.outline(r, rad, t.r(media ? .2f : .13f), media ? hex(0xd9ceff) : white(.95f));
    }
    const HomeItem* c = cur();
    if (!c) return;
    TextStyle nm{t.r(1.8f), Weight::Regular, white(), Align::Left, VAlign::Middle, t.r(.8f)};
    float top = y + t.r(7.45f);
    text::draw(d, c->title, t.r(kRowLeft) + nameX_.value(), top + text::lineHeight(nm.font()) / 2, nm);
}

void Home::drawHero(Draw& d) {
    App& A = app();
    const Theme& t = A.theme;
    const HomeItem* c = cur();
    if (!c) return;
    float x = t.r(11.25f), top = t.r(31);
    float ht = heroT_.value(), mt = metaT_.value();
    bool media = !c->media.empty();
    TextStyle h2 = media ? TextStyle{t.r(3.45f), Weight::Light, white(), Align::Left, VAlign::Top, t.r(1.6f)}
                         : TextStyle{t.r(4.3f), Weight::Bold, white(), Align::Left, VAlign::Top, t.r(1.6f)};
    float lh = h2.size * 1.05f;
    auto lines = text::wrap(c->title, h2.font(), t.r(56), 2);
    d.pushAlpha(ht);
    text::drawLines(d, lines, x, top + t.r(.9f) * (1 - ht), lh, h2);
    d.popAlpha();
    float my = top + lh * lines.size() + t.r(1);
    d.pushAlpha(mt);
    my += t.r(.9f) * (1 - mt);
    if (media) {
        TextStyle st{t.r(1.95f), Weight::Regular, white(.85f), Align::Left, VAlign::Top, t.r(.6f)};
        text::drawLines(d, text::wrap(c->desc, st.font(), t.r(40)), x, my, st.size * 1.4f, st);
    } else {
        struct Span { std::string s; Color c; };
        std::vector<Span> spans;
        if (c->lib) {
            int plays = 0;
            for (auto& g : A.library.all) if (g.status == "Plays") plays++;
            bool showNoArt = A.settings.get("showNoArt", false) == true;
            spans.push_back({std::to_string(A.library.visible(showNoArt).size()) + " games", white(.85f)});
            spans.push_back({std::to_string(plays) + " play", white(.85f)});
        } else {
            const host::Game& g = c->game;
            if (c->running) spans.push_back({"Playing now", col::ok});
            else if (!g.status.empty()) spans.push_back({g.status, g.status == "Plays" ? col::ok : g.status == "Stops early" ? col::warn : white(.85f)});
            if (g.fps) spans.push_back({std::to_string(g.fps) + " FPS on the PC", white(.85f)});
            if (!g.lastRun.empty()) spans.push_back({"Last run " + g.lastRun, white(.85f)});
            if (!g.id.empty()) spans.push_back({g.id, white(.85f)});
        }
        float sx = x;
        for (auto& sp : spans) sx += text::draw(d, sp.s, sx, my, {t.r(1.4f), Weight::Regular, sp.c, Align::Left, VAlign::Top, t.r(.6f)}) + t.r(1.6f);
    }
    d.popAlpha();
}

void Home::drawActs(Draw& d) {
    const Theme& t = app().theme;
    const HomeItem* c = cur();
    if (!c || !c->media.empty()) return;
    bool missing = !c->lib && c->game.exe.empty() && c->game.raw.value("installed", true) == false;
    std::string label = c->lib ? "Open" : c->running ? "Resume" : missing ? "Install" : "Play Game";
    float fs = t.r(1.95f);
    float w = std::max(t.r(18.1f), text::width(label, {fs, Weight::Regular}) + t.r(4.8f));
    float h = t.r(4.4f), x = t.r(11.25f), y = t.r(46.5f);
    widgets::pill(d, {x, y, w, h}, label, fs, focus_ == "acts" && act_ == 0);
    pointer::target({x, y, w, h}, [this] { pickAct(0); }, [this] { pickAct(0); activate(); }, h / 2);
    Rect m{x + w + t.r(1), y, h, h};
    bool ms = focus_ == "acts" && act_ == 1;
    if (ms) widgets::focusGlow(d, m, h / 2);
    d.round(m, h / 2, ms ? white() : rgba(36, 40, 52, .78f));
    pointer::target(m, [this] { pickAct(1); }, [this] { pickAct(1); app().open("gamemenu"); }, h / 2);
    text::draw(d, "\xC2\xB7\xC2\xB7\xC2\xB7", m.x + h / 2, m.y + h / 2, {t.r(1.6f), Weight::Bold, ms ? hex(0x0a0f1e) : white(), Align::Center, VAlign::Middle});
}

void Home::drawSide(Draw& d, const host::Game& g) {
    const Theme& t = app().theme;
    bool running = cur() && cur()->running;
    float w = t.r(23.1f), x = t.W() - t.r(10.9f) - w, y = t.r(22.5f);
    float picH = t.r(14), rad = t.r(.8f);
    TextStyle lab{t.r(1.3f), Weight::Regular, white(.7f)};
    TextStyle sm{t.r(1.25f), Weight::Regular, white(.65f)};
    TextStyle bv{t.r(1.6f), Weight::Semibold, white()};
    float labH = text::lineHeight(lab.font()), smH = text::lineHeight(sm.font()), bH = text::lineHeight(bv.font());
    float h = picH + t.r(.8f) + labH + t.r(.6f) + smH + bH + t.r(1);
    Rect card{x, y, w, h};
    d.round(card, rad, rgba(14, 18, 30, .82f));
    std::string pic = !g.shot.empty() ? g.shot : !g.icon.empty() ? g.icon : g.bg;
    SDL_Texture* tex = pic.empty() ? nullptr : images::get(pic);
    d.pushClip({x, y, w, picH});
    d.round({x, y, w, picH + rad * 2}, rad, hex(0x1c2233));
    if (tex) d.image(tex, {x, y, w, picH + rad * 2}, Fit::Cover, 1, rad);
    d.popClip();
    float ly = y + picH + t.r(.8f);
    text::draw(d, g.shot.empty() ? "Box art" : "Latest capture", x + t.r(1.1f), ly, lab);
    float sy = ly + labH + t.r(.6f);
    std::string status = running ? "Running" : g.status == "Plays" ? "Plays" : g.status == "Stops early" ? "Stops" : g.status == "Not installed" ? "Not installed" : "Untested";
    std::string fps = g.fps ? std::to_string(g.fps) : "\xE2\x80\x93";
    std::pair<const char*, std::string> stats[3] = {{"Status", status}, {"FPS", fps}, {"Saves", std::to_string(g.saves)}};
    float sx = x + t.r(1);
    for (auto& [k, v] : stats) {
        float cw = std::max(text::width(k, sm.font()), text::width(v, bv.font()));
        text::draw(d, k, sx, sy, sm);
        text::draw(d, v, sx, sy + smH, bv);
        sx += cw + t.r(1.6f);
    }
}

void Home::drawShelf(Draw& d, const HomeItem& it) {
    const Theme& t = app().theme;
    const host::Game& g = it.game;
    float x = t.r(10.8f), y = t.r(52);
    TextStyle h4{t.r(1.6f), Weight::Regular, white(), Align::Left, VAlign::Top, t.r(.6f)};
    text::draw(d, "Your activity", x, y, h4);
    y += text::lineHeight(h4.font()) + t.r(.9f);
    struct Card { std::string img, small, big, span; };
    std::vector<Card> cards;
    if (!g.shot.empty()) cards.push_back({g.shot, "Captures", "Latest screenshot", ""});
    cards.push_back({"", "Status", it.running ? "Running now" : g.status.empty() ? "Not tested yet" : g.status,
                     g.fps ? std::to_string(g.fps) + " FPS measured by the pipeline" : "No FPS measured yet"});
    cards.push_back({"", "Saved data", g.saves ? std::to_string(g.saves) + " save file" + (g.saves == 1 ? "" : "s") : "No saves yet", "In the game's _sd folder"});
    if (!g.lastRun.empty()) cards.push_back({"", "Last played or tested", g.lastRun, "From the game's log"});
    cards.push_back({"", "Game info", g.id, "Folder, program and results"});
    float cw = t.r(16), ch = t.r(9), pad = t.r(.8f);
    TextStyle sm{t.r(1.25f), Weight::Regular, white(.7f), Align::Left, VAlign::Top, 0, true};
    TextStyle bg{t.r(1.45f), Weight::Semibold, white(), Align::Left, VAlign::Top};
    TextStyle sp{t.r(1.25f), Weight::Regular, white(.72f), Align::Left, VAlign::Top};
    for (size_t i = 0; i < cards.size(); i++) {
        const Card& c = cards[i];
        Rect r{x + i * (cw + t.r(1.95f)), y, cw, ch};
        if (r.x > t.W()) break;
        bool img = !c.img.empty();
        d.round(r, t.r(.5f), img ? black() : rgba(18, 22, 36, .82f));
        if (img) {
            if (SDL_Texture* tex = images::get(c.img)) d.image(tex, r, Fit::Cover, 1, t.r(.5f));
            sm.shadow = bg.shadow = t.r(.6f);
        } else {
            sm.shadow = bg.shadow = 0;
        }
        float inner = cw - 2 * pad;
        std::vector<std::string> spanLines = c.span.empty() ? std::vector<std::string>() : text::wrap(c.span, sp.font(), inner, 2);
        float spanLh = sp.size * 1.25f, bigLh = bg.size * 1.2f;
        float bottom = r.y + ch - pad;
        float yy = bottom - spanLh * spanLines.size();
        text::drawLines(d, spanLines, r.x + pad, yy, spanLh, sp);
        auto bigLines = text::wrap(c.big, bg.font(), inner, 1);
        yy -= bigLh;
        text::drawLines(d, bigLines, r.x + pad, yy, bigLh, bg);
        yy -= text::lineHeight(sm.font()) + t.r(.1f) - (bigLh - text::lineHeight(bg.font())) / 2;
        text::draw(d, text::ellipsize(text::upper(c.small), sm.font(), inner), r.x + pad, yy, sm);
    }
}

void Home::drawMediaShelf(Draw& d, const HomeItem& it) {
    const Theme& t = app().theme;
    float x = t.r(10.9f), y = t.r(47.3f);
    TextStyle h4{t.r(2.1f), Weight::Regular, white(), Align::Left, VAlign::Top, t.r(.6f)};
    text::draw(d, it.cards.empty() ? "Nothing here yet" : "Featured", x, y, h4);
    y += text::lineHeight(h4.font()) + t.r(.9f);
    float cw = t.r(14.9f), ch = t.r(12.6f), strip = t.r(3.9f), rad = t.r(.5f);
    TextStyle lab{t.r(1.3f), Weight::Semibold, hex(0x111111), Align::Center, VAlign::Middle};
    float sx = x - cardShift_.value();
    if (!it.cards.empty()) pointer::scrollArea({0, y, t.W(), ch}, [this](float rows) { stepCards(rows); });
    float fit = t.W() - t.r(10.9f) - t.r(3);
    for (size_t i = 0; i < it.cards.size() && i < 40; i++) {
        Rect r{sx + i * (cw + t.r(1.95f)), y, cw, ch};
        if (r.x > t.W()) break;
        if (r.x + r.w < 0) continue;
        const MediaCard& c = it.cards[i];
        bool sel = focus_ == "cards" && (int)i == act_;
        if (sel) widgets::focusGlow(d, r, rad);
        bool whole = r.x + r.w <= fit + 1;
        pointer::target(r, [this, i, whole] { if (whole) pickCard((int)i); }, [this, i] { pickCard((int)i); openCard(); }, rad);
        d.round(r, rad, hex(0x111111));
        if (SDL_Texture* tex = images::get(c.thumb.empty() ? c.img : c.thumb)) d.image(tex, r, Fit::Cover, 1, rad, white(), .5f, 0.f);
        d.pushClip({r.x, r.y + ch - strip, cw, strip});
        d.round(r, rad, white());
        d.popClip();
        text::draw(d, text::ellipsize(c.title, lab.font(), cw - t.r(1.4f)), r.x + cw / 2, r.y + ch - strip / 2, lab);
    }
}

void Home::draw(Draw& d) {
    App& A = app();
    const Theme& t = A.theme;
    drawArt(d);
    drawTop(d);
    drawRow(d);
    drawHero(d);
    const bool ccOpen = A.router.isOpen("cc");
    if (!ccOpen) drawActs(d);
    const HomeItem* c = cur();
    if (!c || ccOpen) return;
    if (!c->media.empty()) {
        drawMediaShelf(d, *c);
        return;
    }
    if (c->lib) return;
    drawSide(d, c->game);
    drawShelf(d, *c);
    if (!(A.settings.get("safe", false) == true) && !c->game.folder.empty())
        text::draw(d, c->game.folder, t.r(11.25f), t.H() - t.r(1.4f), {t.r(1.25f), Weight::Regular, white(.45f), Align::Left, VAlign::Baseline});
}

}
