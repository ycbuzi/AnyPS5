#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/collectionScreen.h"
#include "widgets/Widgets.h"
#include <algorithm>

namespace ui {

using collection::Item;
using collection::State;

namespace {
constexpr float kLeft = 10.8f;
const Color kShade = rgba(8, 10, 16);
const Color kAccent = hex(0x6fa8ff);
const char* kDot = "  \xC2\xB7  ";

struct Status {
    std::string head, note;
    Color color;
};

Status statusOf(const Item& it) {
    std::string reason = !it.actionReason.empty() ? it.actionReason : it.compatReason;
    switch (it.state) {
    case State::Installed:
        if (it.update) return {"Update available", it.updateReason.empty() ? collection::buildText(it) : it.updateReason, kAccent};
        return {"Installed", collection::buildText(it), col::ok};
    case State::Installing: return {collection::label(it), it.started.empty() ? "" : "Started " + it.started, white()};
    case State::Failed: return {"Failed", !reason.empty() ? reason : it.status, col::bad};
    case State::Unsupported: return {collection::label(it), reason, col::warn};
    default:
        if (it.compat == "nosource") return {"No game data", "Plug in the drive or connect the PC that has it", col::faint};
        if (!it.actionId.empty() && !it.actionEnabled) return {"Cannot install yet", !it.actionLabel.empty() ? it.actionLabel : reason, col::warn};
        return {"Ready to install", it.compat == "installable" ? "Works on this device" : "", col::dim};
    }
}

std::string subLine(const Item& it) {
    std::string s;
    auto add = [&s](const std::string& p) {
        if (!p.empty()) s += (s.empty() ? "" : kDot) + p;
    };
    add(it.version.empty() ? "" : "Version " + it.version);
    add(collection::sizeText(it.size));
    std::string src = collection::sourcesText(it);
    add(src.empty() ? "" : "From " + src);
    if (s.empty()) s = it.id;
    return s;
}
}

void CollectionScreen::drawBg(Draw& d) {
    const Theme& t = app().theme;
    Rect full{0, 0, t.W(), t.H()};
    d.rect(full, hex(0x0b0d13));
    d.linear(full, 90, {{0, alpha(kShade, .96f)}, {.5f, alpha(hex(0x141a2a), .9f)}, {1, alpha(hex(0x1a2440), .85f)}});
    d.linear(full, 0, {{0, alpha(kShade, .9f)}, {.4f, alpha(kShade, 0)}, {1, alpha(kShade, 0)}});
}

void CollectionScreen::drawHead(Draw& d) {
    const Theme& t = app().theme;
    float hy = t.r(5.4f);
    d.icon(images::icon("box", t.r(2.4f)), t.r(4.3f + 1.2f), hy, t.r(2.4f));
    text::draw(d, "Your Collection", t.r(kLeft), hy, {t.r(2.4f), Weight::Light, white(), Align::Left, VAlign::Middle});
    TextStyle small{t.r(1.4f), Weight::Regular, white(.72f), Align::Left, VAlign::Middle};
    std::string sub;
    if (!ids_.empty()) {
        int inst = 0;
        for (auto& id : ids_)
            if (const Item* it = collection::byId(id); it && it->state == State::Installed) inst++;
        sub = std::to_string(ids_.size()) + (ids_.size() == 1 ? " game" : " games") + kDot + std::to_string(inst) + " installed";
        std::vector<std::string> media;
        for (auto& it : collection::items())
            for (auto& s : it.sources) {
                std::string n = s.type == "media" ? "USB drive" : collection::sourceName(s);
                if (std::find(media.begin(), media.end(), n) == media.end()) media.push_back(n);
            }
        if (!media.empty()) {
            std::string m;
            for (auto& x : media) m += (m.empty() ? "" : ", ") + x;
            sub += std::string(kDot) + "From " + m;
        }
    } else {
        sub = "Games from this device, USB drives and your PC";
    }
    text::draw(d, sub, t.r(kLeft), t.r(8.9f), small);
    std::string dev = collection::deviceText();
    if (!dev.empty()) {
        small.align = Align::Right;
        text::draw(d, "This device: " + dev, t.W() - t.r(kLeft), t.r(8.9f), small);
    }
}

void CollectionScreen::drawBar(Draw& d, float y, bool centred) {
    const Theme& t = app().theme;
    auto b = buttons();
    float px = t.r(1.4f), h = t.r(3.2f), gap = t.r(1.2f);
    std::vector<float> ws;
    float total = 0;
    for (auto& x : b) {
        ws.push_back(std::max(t.r(10), text::width(x.label, {px, Weight::Regular}) + t.r(3.2f)));
        total += ws.back() + gap;
    }
    total -= gap;
    float x = centred ? (t.W() - total) / 2 : t.r(kLeft);
    for (int i = 0; i < (int)b.size(); i++) {
        Rect r{x, y, ws[i], h};
        bool on = onBar_ && bar_ == i;
        widgets::pill(d, r, b[i].label, px, on);
        pointer::target(r, [this, i] {
            onBar_ = true;
            bar_ = i;
        }, [this, i] {
            onBar_ = true;
            bar_ = i;
            auto bs = buttons();
            if (i < (int)bs.size() && bs[i].run) bs[i].run();
        }, h / 2);
        x += ws[i] + gap;
    }
}

void CollectionScreen::drawRow(Draw& d, const Item& it, Rect r, int i) {
    const Theme& t = app().theme;
    bool on = !onBar_ && i == sel_;
    float rad = t.r(.6f);
    if (on) widgets::focusGlow(d, r, rad);
    d.round(r, rad, on ? rgba(44, 52, 74, .95f) : white(.05f));
    pointer::target(r, [this, i] {
        onBar_ = false;
        sel_ = i;
    }, [this, i] {
        onBar_ = false;
        sel_ = i;
        activate(i);
    }, rad);
    float pad = t.r(.9f), is = r.h - pad * 2;
    Rect ir{r.x + pad, r.y + pad, is, is};
    d.round(ir, t.r(.4f), hex(0x1c2233));
    std::string icon = it.icon;
    if (const host::Game* g = app().library.byId(it.id); g && !g->icon.empty()) icon = g->icon;
    bool dim = it.state == State::Unsupported || (it.state == State::NotInstalled && it.compat == "nosource");
    SDL_Texture* tex = icon.empty() ? nullptr : images::get(icon);
    if (tex) d.image(tex, ir, Fit::Cover, dim ? .45f : 1.f, t.r(.4f));
    else text::draw(d, widgets::initial(json{{"name", it.title.empty() ? it.id : it.title}}), ir.x + is / 2, ir.y + is / 2, {t.r(2.2f), Weight::Bold, white(dim ? .5f : 1.f), Align::Center, VAlign::Middle});
    Status st = statusOf(it);
    st.note = collection::sentence(st.note);
    std::string verb = collection::actionVerb(it);
    bool button = !verb.empty() && (it.actionId.empty() || it.actionEnabled || verb == "Cancel");
    float right = r.x + r.w - t.r(1.4f);
    TextStyle bs{t.r(1.35f), Weight::Medium, white(), Align::Center, VAlign::Middle};
    if (button) {
        float bw = std::max(t.r(9.5f), text::width(verb, bs.font()) + t.r(3)), bh = t.r(3);
        Rect br{right - bw, r.y + (r.h - bh) / 2, bw, bh};
        bool primary = verb == "Install" || verb == "Update" || verb == "Retry";
        if (on) {
            d.round(br, bh / 2, white());
            bs.color = hex(0x0a0f1e);
        } else {
            d.round(br, bh / 2, primary ? alpha(kAccent, .35f) : white(.12f));
            d.stroke(br, bh / 2, std::max(1.f, t.r(.08f)), primary ? alpha(kAccent, .8f) : white(.3f));
        }
        text::draw(d, verb, br.x + bw / 2, br.y + bh / 2, bs);
        right = br.x - t.r(2);
    }
    float statusW = t.r(34);
    float sx = right;
    TextStyle hs{t.r(1.5f), Weight::Medium, st.color, Align::Right, VAlign::Middle};
    TextStyle ns{t.r(1.2f), Weight::Regular, white(.6f), Align::Right, VAlign::Middle};
    float cy = r.y + r.h / 2;
    bool two = !st.note.empty() || it.state == State::Installing;
    text::draw(d, text::ellipsize(st.head, hs.font(), statusW), sx, two ? cy - t.r(1.05f) : cy, hs);
    if (it.state == State::Installing) {
        float bw = t.r(20);
        Rect bar{sx - bw, cy + t.r(.9f), bw, t.r(.45f)};
        d.round(bar, bar.h / 2, white(.15f));
        if (it.pct > 0) d.round({bar.x, bar.y, bar.w * it.pct / 100.f, bar.h}, bar.h / 2, white());
    } else if (!st.note.empty()) {
        text::draw(d, text::ellipsize(st.note, ns.font(), statusW), sx, cy + t.r(1.2f), ns);
    }
    float tx = ir.x + is + t.r(1.6f);
    float tw = sx - statusW - t.r(2) - tx;
    tw = std::max(tw, t.r(20));
    TextStyle ts{t.r(1.75f), Weight::Regular, white(dim ? .65f : 1.f), Align::Left, VAlign::Middle};
    text::draw(d, text::ellipsize(it.title.empty() ? it.id : it.title, ts.font(), tw), tx, cy - t.r(1.05f), ts);
    TextStyle ss{t.r(1.25f), Weight::Regular, white(.6f), Align::Left, VAlign::Middle};
    text::draw(d, text::ellipsize(subLine(it), ss.font(), tw), tx, cy + t.r(1.2f), ss);
}

void CollectionScreen::drawEmpty(Draw& d) {
    const Theme& t = app().theme;
    float cx = t.W() / 2, y = t.H() * .34f;
    if (!collection::loaded()) {
        text::draw(d, "Looking for your games\xE2\x80\xA6", cx, y + t.r(4), {t.r(1.8f), Weight::Regular, white(.8f), Align::Center, VAlign::Middle});
        return;
    }
    if (!collection::supported()) {
        text::draw(d, "Your Collection needs the AnyPS5 OS host", cx, y + t.r(4), {t.r(1.8f), Weight::Regular, white(.8f), Align::Center, VAlign::Middle});
        return;
    }
    d.circle(cx, y, t.r(4.2f), white(.08f));
    d.icon(images::icon("box", t.r(4)), cx, y, t.r(4));
    y += t.r(7.4f);
    text::draw(d, "No games yet", cx, y, {t.r(2.6f), Weight::Light, white(), Align::Center, VAlign::Middle});
    y += t.r(3.6f);
    text::draw(d, "Plug in a drive with your games, or connect to your PC.", cx, y, {t.r(1.6f), Weight::Regular, white(.78f), Align::Center, VAlign::Middle});
    y += t.r(2.6f);
    std::string srv = collection::server();
    std::string hint = srv.empty() ? "On the PC, start the AnyPS5 library server, then enter its address here." : "PC library: " + srv;
    text::draw(d, hint, cx, y, {t.r(1.3f), Weight::Regular, white(.55f), Align::Center, VAlign::Middle});
    std::string err = collection::listError();
    if (!err.empty()) {
        y += t.r(2.2f);
        TextStyle es{t.r(1.3f), Weight::Regular, col::warn, Align::Center, VAlign::Middle};
        text::draw(d, text::ellipsize(err, es.font(), t.W() - t.r(20)), cx, y, es);
    }
    drawBar(d, y + t.r(3.2f), true);
}

void CollectionScreen::draw(Draw& d) {
    const Theme& t = app().theme;
    drawBg(d);
    drawHead(d);
    if (ids_.empty()) {
        drawEmpty(d);
        return;
    }
    drawBar(d, t.r(11.6f), false);
    std::string err = collection::listError();
    if (!err.empty()) {
        TextStyle es{t.r(1.25f), Weight::Regular, col::warn, Align::Right, VAlign::Middle};
        text::draw(d, text::ellipsize(err, es.font(), t.r(60)), t.W() - t.r(kLeft), t.r(11.6f + 1.6f), es);
    }
    Rect v = listRect();
    pointer::scrollArea(v, [this](float rows) { scrollRows(rows); });
    d.pushClip(v);
    float sc = scroll_.value();
    for (int i = 0; i < (int)ids_.size(); i++) {
        Rect r = rowRect(i, sc);
        if (r.y > v.y + v.h || r.y + r.h < v.y) continue;
        if (const Item* it = collection::byId(ids_[i])) drawRow(d, *it, r, i);
    }
    d.popClip();
}

}
