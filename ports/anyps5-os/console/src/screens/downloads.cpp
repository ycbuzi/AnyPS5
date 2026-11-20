#include "core/Pointer.h"
#include "screens/control_center.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ui::cc {

namespace {

std::vector<DlItem> items;
bool paused = false;

std::string fold(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (std::isalnum(c) || c >= 0x80) o += (char)std::tolower(c);
    return o;
}

std::string str(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

}

const std::vector<DlItem>& dlItems() { return items; }
bool dlPaused() { return paused; }

void dlLoad(std::function<void()> done) {
    app().host.get("cache/downloads.js?" + std::to_string(app().wallNow()), [done](int status, const std::string& body) {
        if (status == 200) {
            json o = host::parseJsAssignment(body);
            std::vector<DlItem> v;
            if (o.is_object() && o.contains("items") && o["items"].is_array())
                for (auto& x : o["items"]) {
                    if (!x.is_object()) continue;
                    DlItem d{str(x, "title"), str(x, "kind"), str(x, "status"), str(x, "started"), str(x, "gameId")};
                    if (x.contains("pct") && x["pct"].is_number()) d.pct = std::max(0, std::min(100, (int)x["pct"].get<double>()));
                    v.push_back(d);
                }
            items = v;
            paused = o.is_object() && o.value("paused", false);
        }
        app().host.sys("collection", "status", json::object(), [done](const json& r) {
            items.erase(std::remove_if(items.begin(), items.end(), [](const DlItem& d) { return d.kind == "collection"; }), items.end());
            const json* jobs = r.contains("data") && r["data"].is_object() && r["data"].contains("jobs") ? &r["data"]["jobs"] : nullptr;
            if (jobs && jobs->is_array())
                for (auto& x : *jobs) {
                    if (!x.is_object()) continue;
                    std::string st = str(x, "state");
                    if (st == "installed" || st == "none") continue;
                    DlItem d{str(x, "title"), "collection", st == "downloading" ? "Installing" : st == "interrupted" ? "Paused" : st, "", str(x, "titleId")};
                    if (x.contains("percent") && x["percent"].is_number()) d.pct = std::max(0, std::min(100, (int)x["percent"].get<double>()));
                    items.push_back(d);
                }
            if (done) done();
        });
    });
}

std::string dlKind(const std::string& k) {
    if (k == "download") return "Copying from the PS5";
    if (k == "install") return "Extracting";
    if (k == "collection") return "Installing from your collection";
    if (k == "job") return "Console job";
    if (k == "test") return "Testing";
    if (k == "build") return "Building";
    if (k == "machine") return "Other machine";
    return "";
}

std::string dlGlyph(const std::string& k) {
    if (k == "test") return "dl-test";
    if (k == "build") return "dl-build";
    if (k == "machine") return "dl-machine";
    return "cc-download";
}

const host::Game* dlGame(const DlItem& x) {
    if (!x.gameId.empty())
        if (const host::Game* g = app().library.byId(x.gameId)) return g;
    std::string t = x.title;
    const std::string pre = "Building AnyPS5 for ";
    if (t.rfind(pre, 0) == 0) t = t.substr(pre.size());
    t = fold(t);
    for (auto& g : app().library.all)
        if (fold(g.title) == t) return &g;
    return nullptr;
}

namespace {

struct Row {
    std::string icon, glyph, small, title, status;
    int pct = -1;
    bool bar = false, link = false;
    Run run;
};

void openMachines() {
    app().router.close("cc");
    app().host.send(json{{"machines", true}});
}

class DownloadsPanel : public Panel {
public:
    DownloadsPanel(float ax, int s) {
        head = "Downloads";
        anchorX = ax;
        int n = std::min(5, (int)items.size());
        build(n);
        while (n > 1 && height() > maxH()) build(--n);
        sel = std::max(0, std::min(s, count() - 1));
    }
    float maxH() const { return app().theme.H() - app().theme.r(16.7f); }
    void build(int n) {
        rows_.clear();
        for (int i = 0; i < n; i++) {
            const DlItem& x = items[i];
            const host::Game* g = dlGame(x);
            std::string k = dlKind(x.kind);
            rows_.push_back({g ? g->icon : "", dlGlyph(x.kind), (k.empty() ? "Working" : k) + (x.started.empty() ? "" : " \xC2\xB7 since " + x.started), x.title, x.status, x.pct, true, false, {}});
        }
        if (rows_.empty()) rows_.push_back({"", "cc-download", "", "Nothing is downloading or running", "Copies from your PS5, builds and tests show up here.", -1, false, false, {}});
        if ((int)items.size() > n) rows_.push_back({"", "", "", std::to_string(items.size() - n) + " more", "See everything in the Machines window", -1, false, true, openMachines});
        rows_.push_back({"", "dl-machine", "", "Open the Machines Window", "The pipeline, every machine and its jobs", -1, false, true, openMachines});
    }
    int count() const override { return (int)rows_.size(); }
    float width() const override { return std::min(app().theme.r(40.8f), app().theme.W() - app().theme.r(4)); }
    float headH() const {
        const Theme& t = app().theme;
        return t.r(2.5f) + text::lineHeight({t.r(1.5f), Weight::Regular});
    }
    float textH(const Row& r) const {
        const Theme& t = app().theme;
        float h = 0;
        int parts = 0;
        if (!r.small.empty()) h += text::lineHeight({t.r(1.25f), Weight::Regular}), parts++;
        h += text::lineHeight({t.r(1.7f), Weight::Regular}), parts++;
        if (!r.status.empty()) h += text::lineHeight({t.r(1.25f), Weight::Regular}), parts++;
        if (r.bar) h += t.r(.8f), parts++;
        return h + t.r(.2f) * (parts - 1);
    }
    float rowH(const Row& r) const {
        const Theme& t = app().theme;
        float pad = r.link ? t.r(1.4f) : t.r(1.1f);
        float body = std::max(textH(r), r.glyph.empty() && r.icon.empty() ? 0.f : t.r(4.6f));
        return 2 * pad + body;
    }
    float height() const override {
        float h = headH() + app().theme.r(.6f);
        for (auto& r : rows_) h += rowH(r);
        return h;
    }
    Run pick() override { return rows_[sel].run; }
    void draw(Draw& d, Rect r) override {
        const Theme& t = app().theme;
        Font fh{t.r(1.5f), Weight::Regular};
        text::draw(d, head, r.x + t.r(1.6f), r.y + t.r(1.5f), {fh.size, fh.weight, white(.75f), Align::Left, VAlign::Top});
        if (paused) text::draw(d, "Pipeline paused", r.x + r.w - t.r(1.6f), r.y + t.r(1.5f), {fh.size, fh.weight, hex(0xffb35c), Align::Right, VAlign::Top});
        float y = r.y + headH();
        for (int i = 0; i < count(); i++) {
            const Row& w = rows_[i];
            Rect row{r.x, y, r.w, rowH(w)};
            d.rect({r.x, y + row.h - t.r(.06f), r.w, t.r(.06f)}, white(.08f));
            if (i == sel) drawRowSel(d, row);
            float x = r.x + t.r(1.5f), right = r.x + r.w - t.r(1.5f), cy = y + row.h / 2;
            if (!w.glyph.empty() || !w.icon.empty()) {
                float s = t.r(4.6f);
                Rect ir{x, cy - s / 2, s, s};
                SDL_Texture* tex = w.icon.empty() ? nullptr : images::get(w.icon);
                d.round(ir, t.r(.2f), hex(0x2a2d38));
                if (tex) d.image(tex, ir, Fit::Cover, 1, t.r(.2f));
                else if (SDL_Texture* g = images::icon(w.glyph, t.r(2.4f))) d.icon(g, ir.x + s / 2, cy, t.r(2.4f));
                x += s + t.r(1.4f);
            }
            if (w.pct >= 0) {
                std::string p = std::to_string(w.pct) + "%";
                Font fp{t.r(1.4f), Weight::Regular};
                text::draw(d, p, right, cy, {fp.size, fp.weight, white(.8f), Align::Right, VAlign::Middle});
                right -= text::width(p, fp) + t.r(1.4f);
            }
            drawText(d, w, x, right - x, cy - textH(w) / 2);
            pointer::target(row, [this, i] { sel = i; }, [this, i] {
                sel = i;
                app().act({"ok", ""});
            });
            y += row.h;
        }
    }
    void drawText(Draw& d, const Row& w, float x, float tw, float y) {
        const Theme& t = app().theme;
        Font fs{t.r(1.25f), Weight::Regular}, fb{t.r(1.7f), Weight::Regular};
        if (!w.small.empty()) {
            text::draw(d, text::ellipsize(w.small, fs, tw), x, y, {fs.size, fs.weight, white(.6f), Align::Left, VAlign::Top});
            y += text::lineHeight(fs) + t.r(.2f);
        }
        text::draw(d, text::ellipsize(w.title, fb, tw), x, y, {fb.size, fb.weight, white(), Align::Left, VAlign::Top});
        y += text::lineHeight(fb) + t.r(.2f);
        if (!w.status.empty()) {
            text::draw(d, text::ellipsize(w.status, fs, tw), x, y, {fs.size, fs.weight, white(.7f), Align::Left, VAlign::Top});
            y += text::lineHeight(fs) + t.r(.2f);
        }
        if (!w.bar) return;
        Rect bar{x, y + t.r(.5f), tw, t.r(.3f)};
        d.round(bar, t.r(.15f), white(.18f));
        if (w.pct >= 0) {
            if (w.pct > 0) d.round({bar.x, bar.y, bar.w * w.pct / 100.f, bar.h}, t.r(.15f), white());
            return;
        }
        float seg = bar.w * .3f, ph = t.reduceMotion ? .3f : (float)std::fmod(now(), 1.6) / 1.6f;
        float sx = bar.x + seg * (-1 + 4.4f * ph);
        d.pushClip(bar);
        d.rect({sx, bar.y, seg, bar.h}, white());
        d.popClip();
    }

private:
    std::vector<Row> rows_;
};

}

std::unique_ptr<Panel> downloads(float anchorX, int sel) { return std::make_unique<DownloadsPanel>(anchorX, sel); }

}
