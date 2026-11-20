#include "core/Glyphs.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/library.h"
#include "widgets/Widgets.h"
#include <algorithm>
#include <cmath>

namespace ui {

namespace {

struct Row {
    std::string label, value;
    bool secret = false;
};

LibEntry g_entry;

Color radial(float x, float y, float cx, float cy, float rx, float ry, Color c) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    float d = std::sqrt(dx * dx + dy * dy) / .7f;
    return d >= 1 ? alpha(c, 0) : alpha(c, 1 - d);
}

Color over(Color base, Color top) {
    float a = top.a;
    return {base.r + (top.r - base.r) * a, base.g + (top.g - base.g) * a, base.b + (top.b - base.b) * a, 1};
}

}

class GameInfo : public Screen {
public:
    bool overlay() const override { return true; }

    void enter() override {
        sel_ = 0;
        scroll_.snap(0);
        build();
    }

    void host(const json& m) override {
        if (m.value("collectionChanged", false) || m.value("libraryLoaded", false)) build();
    }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        int n = (int)rows_.size();
        if (k == "up" && sel_ > 0) sel_--;
        else if (k == "down" && sel_ < n - 1) sel_++;
        else if (k == "options") {
            const Theme& t = app().theme;
            if (!g_entry.g.id.empty()) psmenu::show(psmenu::forEntry(collectionEntryFor(), false), {t.r(10.5f), t.r(11.2f), t.r(22.9f), t.r(22.9f)}, true);
        }
        else if (k == "back" || k == "ok" || k == "enter" || k == "ps") app().router.pop();
        return true;
    }

    std::vector<Hint> legend() const override { return {{"Back", "back"}, {"Options", "options"}}; }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        drawBg(d);
        text::draw(d, "Information", t.r(6), t.r(6.4f), {t.r(2.85f), Weight::Light, white(), Align::Left, VAlign::Middle});
        drawArt(d);
        drawCompat(d);
        float x = t.r(39.2f), w = t.W() - t.r(6) - x, top = t.r(11.2f);
        TextStyle ts{t.r(2.6f), Weight::Light, white(), Align::Left, VAlign::Middle};
        text::draw(d, text::ellipsize(g_entry.g.title, ts.font(), w - t.r(2.8f)), x + t.r(1.4f), top + t.r(2.6f * 1.25f) / 2, ts);
        float rowsTop = top + t.r(2.6f * 1.25f + 1.6f), rowH = t.r(6.2f);
        float limit = t.H() - t.r(5.6f);
        float want = std::max(0.f, (sel_ + 1) * rowH - (limit - rowsTop));
        if (std::fabs(want - scroll_.to) > .5f) scroll_.set(want, .2f, easeOut);
        d.pushClip({x - t.r(.5f), rowsTop - t.r(.3f), w + t.r(1), limit - rowsTop + t.r(.3f)});
        float y = rowsTop - scroll_.value();
        bool safe = app().settings.get("safe", false) == true;
        pointer::scrollArea({x, rowsTop, w, limit - rowsTop}, [this](float rows) {
            int n = (int)rows_.size();
            sel_ = std::clamp(sel_ + (rows > 0 ? 1 : -1), 0, std::max(0, n - 1));
        });
        for (int i = 0; i < (int)rows_.size(); i++, y += rowH) {
            Rect r{x, y, w, rowH};
            if (y + rowH < rowsTop - t.r(.3f) || y > limit) continue;
            pointer::target(r, [this, i] { sel_ = i; }, [this, i] { sel_ = i; }, t.r(.25f));
            if (i == sel_) {
                d.round(r, t.r(.25f), white(.07f));
                d.outline(r, t.r(.25f), std::max(1.f, t.r(.1f)), white(.85f));
            } else {
                d.rect({r.x, r.y + r.h - std::max(1.f, t.r(.06f)), r.w, std::max(1.f, t.r(.06f))}, white(.09f));
            }
            text::draw(d, rows_[i].label, r.x + t.r(1.4f), r.y + r.h / 2, {t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Middle});
            TextStyle vs{t.r(1.6f), Weight::Regular, white(.62f), Align::Right, VAlign::Middle};
            std::string v = text::ellipsize(rows_[i].value, vs.font(), (r.w - t.r(2.8f)) * .62f);
            float vx = r.x + r.w - t.r(1.4f), vy = r.y + r.h / 2;
            if (rows_[i].secret && safe) blurred(d, v, vx, vy, vs);
            else text::draw(d, v, vx, vy, vs);
        }
        d.popClip();
        glyph::line(d, glyph::has("back") ? "{back} goes back" : "Right-click or the Back button goes back", t.W() / 2, t.H() - t.r(3.4f),
                    {t.r(1.3f), Weight::Regular, white(.6f), Align::Center, VAlign::Middle});
    }

private:
    void build() {
        const LibEntry& e = g_entry;
        const host::Game& g = e.g;
        bool running = g.raw.is_object() && g.raw.value("running", false);
        if (const host::Game* pg = app().playingGame(); pg && !g.id.empty() && pg->id == g.id) running = true;
        rows_.clear();
        rows_.push_back({"Status", running ? "Running now" : g.status.empty() ? "Not tested yet" : g.status});
        std::string inst = collection::supported() ? collection::labelFor(g.id) : "";
        if (!inst.empty()) rows_.push_back({"Installation", inst});
        if (const collection::Item* ci = collection::supported() ? collection::byId(g.id) : nullptr) {
            if (ci->build.is_object() || ci->state == collection::State::Installed) rows_.push_back({"Installed build", collection::buildText(*ci)});
            if (!ci->version.empty()) rows_.push_back({"Game version", ci->version});
            std::string src = collection::sourcesText(*ci);
            if (!src.empty()) rows_.push_back({"Game data on", collection::sentence(src)});
            if (ci->size >= 0) rows_.push_back({"Size", collection::sizeText(ci->size)});
        }
        rows_.push_back({"Frame rate", g.fps ? std::to_string(g.fps) + " FPS on this PC" : "Not measured"});
        rows_.push_back({"Title ID", g.id.empty() ? "Unknown" : g.id});
        rows_.push_back({"Last played", g.lastRun.empty() ? "Never" : g.lastRun});
        rows_.push_back({"Saved data", std::to_string(g.saves) + " files"});
        rows_.push_back({"Folder", g.folder.empty() ? "Unknown" : g.folder, true});
        rows_.push_back({"Program", g.exe.empty() ? "Not built yet" : g.exe, true});
        sel_ = std::min(sel_, (int)rows_.size() - 1);
    }

    void drawBg(Draw& d) {
        const Theme& t = app().theme;
        float W = t.W(), H = t.H();
        SDL_Texture* bg = d.cached("gameinfo-bg", (int)W, (int)H, [W, H](Draw& c) {
            c.field({0, 0, W, H}, 96, 54, [W, H](float x, float y) {
                float k = y / H;
                Color base = mix(hex(0x191b21), hex(0x0f1117), k);
                base = over(base, radial(x, y, W * .30f, H * 1.06f, W * .90f, H * .28f, white(.08f)));
                base = over(base, radial(x, y, W * .88f, H * .55f, W * .60f, H * .55f, rgba(36, 56, 104, .38f)));
                base = over(base, radial(x, y, W * .13f, H * -.08f, W * .36f, H * .55f, rgba(255, 236, 214, .22f)));
                return base;
            });
        });
        if (bg) d.image(bg, {0, 0, W, H}, Fit::Fill);
        else d.rect({0, 0, W, H}, hex(0x15171d));
    }

    void drawArt(Draw& d) {
        const Theme& t = app().theme;
        Rect r{t.r(10.5f), t.r(11.2f), t.r(22.9f), t.r(22.9f)};
        float rad = t.r(.4f);
        d.round(r, rad, hex(0x2a2d38));
        SDL_Texture* tex = g_entry.g.icon.empty() ? nullptr : images::get(g_entry.g.icon);
        if (tex) d.image(tex, r, Fit::Cover, 1, rad);
        else if (g_entry.g.icon.empty())
            text::draw(d, widgets::initial(json{{"name", g_entry.g.title}}), r.x + r.w / 2, r.y + r.h / 2, {t.r(7), Weight::Semibold, white(.5f), Align::Center, VAlign::Middle});
    }

    static LibEntry collectionEntryFor() {
        if (const collection::Item* ci = collection::byId(g_entry.g.id); ci && !g_entry.inLib) return collectionEntry(*ci);
        return g_entry;
    }

    void drawCompat(Draw& d) {
        const Theme& t = app().theme;
        const collection::Item* ci = collection::supported() ? collection::byId(g_entry.g.id) : nullptr;
        if (!ci || ci->compat.empty()) return;
        Rect r{t.r(10.5f), t.r(35.6f), t.r(22.9f), 0};
        float pad = t.r(1.2f), w = r.w - pad * 2;
        std::string head = ci->compat == "installable" ? "Works on this device" : ci->compat == "unsupported" ? "Not supported on this device"
                         : ci->compat == "needs-newer" ? "Needs a newer AnyPS5" : "No game data found";
        Color hc = ci->compat == "installable" ? col::ok : ci->compat == "nosource" ? white(.7f) : col::warn;
        std::string body = ci->compat == "installable" ? "AnyPS5 builds this game here, for this device's processor and system, so it always matches."
                         : ci->compat == "nosource" ? "Plug in the drive with this game, or connect to your PC."
                         : ci->compatReason.empty() ? collection::compatText(*ci) : collection::sentence(ci->compatReason) + ".";
        TextStyle hs{t.r(1.6f), Weight::Medium, hc, Align::Left, VAlign::Top};
        TextStyle bs{t.r(1.3f), Weight::Regular, white(.78f), Align::Left, VAlign::Top};
        TextStyle ls{t.r(1.15f), Weight::Regular, white(.5f), Align::Left, VAlign::Top};
        auto hl = text::wrap(head, hs.font(), w, 2);
        auto bl = text::wrap(body, bs.font(), w, 6);
        std::string dev = collection::deviceText();
        auto dl = dev.empty() ? std::vector<std::string>{} : text::wrap("This device: " + dev, ls.font(), w, 2);
        float hh = hs.size * 1.3f, bh = bs.size * 1.45f, lh = ls.size * 1.45f;
        r.h = pad * 2 + hl.size() * hh + t.r(.5f) + bl.size() * bh + (dl.empty() ? 0 : t.r(.8f) + dl.size() * lh);
        r.h = std::min(r.h, t.H() - t.r(6) - r.y);
        d.round(r, t.r(.4f), white(.06f));
        d.rect({r.x, r.y + t.r(.4f), std::max(2.f, t.r(.25f)), r.h - t.r(.8f)}, hc);
        d.pushClip(r);
        float y = r.y + pad;
        text::drawLines(d, hl, r.x + pad, y, hh, hs);
        y += hl.size() * hh + t.r(.5f);
        text::drawLines(d, bl, r.x + pad, y, bh, bs);
        y += bl.size() * bh + t.r(.8f);
        if (!dl.empty()) text::drawLines(d, dl, r.x + pad, y, lh, ls);
        d.popClip();
    }

    void blurred(Draw& d, const std::string& v, float x, float y, TextStyle st) {
        float s = app().theme.r(.25f);
        st.color = white(.62f * .14f);
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++) text::draw(d, v, x + i * s * 1.6f, y + j * s, st);
    }

    std::vector<Row> rows_;
    int sel_ = 0;
    Tween scroll_;
};

ANYPS5_SCREEN(GameInfo, "gameinfo");

namespace gameinfo {
void show(const LibEntry& e) {
    g_entry = e;
    if (Screen* s = app().router.get("gameinfo"); s && app().router.isOpen("gameinfo")) s->enter();
    else app().open("gameinfo");
}
}

}
