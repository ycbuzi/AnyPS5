#include "screens/control_center.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::cc {

namespace {

class NotesPanel : public Panel {
public:
    NotesPanel(PanelHost& host, float ax, int s) : host_(&host) {
        head = "Notifications";
        anchorX = ax;
        sel = std::max(0, std::min(s, count() - 1));
        moved();
    }
    std::vector<Note>& list() const { return app().toasts.history(); }
    int count() const override { return 1 + std::max(1, (int)list().size()); }
    float width() const override { return std::min(app().theme.r(40.8f), app().theme.W() - app().theme.r(4)); }
    float headH() const {
        const Theme& t = app().theme;
        return t.r(2.5f) + text::lineHeight({t.r(1.5f), Weight::Regular});
    }
    float rowH(int i) const { return app().theme.r(i == 0 ? 9.2f : 8); }
    float avail() const { return app().theme.H() - app().theme.r(16.7f) - headH() - app().theme.r(.6f); }
    int last() const {
        float used = 0;
        int i = top_;
        for (; i < count(); i++) {
            if (i > top_ && used + rowH(i) > avail()) break;
            used += rowH(i);
        }
        return i;
    }
    float height() const override {
        float h = headH() + app().theme.r(.6f);
        for (int i = top_; i < last(); i++) h += rowH(i);
        return h;
    }
    void moved() override {
        if (sel < top_) top_ = sel;
        auto sum = [&](int a, int b) { float s = 0; for (int i = a; i <= b; i++) s += rowH(i); return s; };
        while (top_ < sel && sum(top_, sel) > avail()) top_++;
    }
    Run pick() override {
        PanelHost* h = host_;
        float ax = anchorX;
        if (sel == 0) return [h, ax] { setDnd(!dndOn()); h->open(notes(*h, ax, 0)); };
        if (list().empty()) return [h, ax] { h->open(notes(*h, ax, 1)); };
        return {};
    }
    bool key(const Action& a) override {
        PanelHost* h = host_;
        float ax = anchorX;
        if (a.is("square") && sel > 0 && sel - 1 < (int)list().size()) {
            list().erase(list().begin() + (sel - 1));
            int keep = std::min(sel, (int)list().size());
            h->open(notes(*h, ax, keep));
            return true;
        }
        if (a.is("options")) {
            h->open(menu("Notifications",
                         {{"Delete All Notifications", [h, ax] { app().toasts.history().clear(); h->open(notes(*h, ax, 0)); }},
                          {"Cancel", [h, ax] { h->open(notes(*h, ax, 0)); }}},
                         Place::Cc, ax));
            return true;
        }
        return false;
    }
    void drawText(Draw& d, float x, float w, float y, float h, const std::string& small, const std::string& big, float bigPx, bool bigFirst) {
        const Theme& t = app().theme;
        Font fs{t.r(1.3f), Weight::Regular}, fb{bigPx, Weight::Regular};
        float hs = text::lineHeight(fs), hb = text::lineHeight(fb), gap = t.r(.3f);
        float ty = y + (h - hs - hb - gap) / 2;
        TextStyle ss{fs.size, fs.weight, white(.65f), Align::Left, VAlign::Top};
        TextStyle sb{fb.size, fb.weight, white(), Align::Left, VAlign::Top};
        if (bigFirst) {
            text::draw(d, text::ellipsize(big, fb, w), x, ty, sb);
            text::draw(d, text::ellipsize(small, fs, w), x, ty + hb + gap, ss);
        } else {
            text::draw(d, text::ellipsize(small, fs, w), x, ty, ss);
            text::draw(d, text::ellipsize(big, fb, w), x, ty + hs + gap, sb);
        }
        d.rect({x, y + h - t.r(.06f), w, t.r(.06f)}, white(.1f));
    }
    void draw(Draw& d, Rect r) override {
        const Theme& t = app().theme;
        text::draw(d, head, r.x + t.r(1.6f), r.y + t.r(1.5f), {t.r(1.5f), Weight::Regular, white(.75f), Align::Left, VAlign::Top});
        float y = r.y + headH(), pad = t.r(1.5f), gap = t.r(1.4f);
        int end = last();
        for (int i = top_; i < end; i++) {
            Rect row{r.x, y, r.w, rowH(i)};
            if (i == sel) drawRowSel(d, row);
            pointer::target(row, [this, i] { hoverRow(i); }, [this, i] { hoverRow(i); app().act({"ok", ""}); });
            float x = r.x + pad, right = r.x + r.w - pad;
            if (i == 0) {
                float tw = t.r(3.6f);
                drawText(d, x, right - tw - gap - x, y, row.h, "This will stay on until you log out of AnyPS5.", "Do Not Disturb", t.r(1.75f), true);
                drawToggle(d, right - tw, y + row.h / 2, dndOn());
            } else if (list().empty()) {
                drawText(d, x, right - x, y, row.h, "New ones appear here, newest first.", "No notifications", t.r(1.85f), true);
            } else {
                const Note& n = list()[i - 1];
                float ic = t.r(4.2f);
                Rect ir{x, y + (row.h - ic) / 2, ic, ic};
                if (!n.icon.empty()) drawThumb(d, ir, t.r(.2f), n.icon, hex(0x14182a));
                else {
                    d.round(ir, t.r(.2f), hex(0x14182a));
                    SDL_Texture* logo = images::asset("logo-mark.png");
                    if (logo) d.image(logo, inset(ir, ic * .19f), Fit::Contain);
                }
                std::string ag = age(n.wall);
                Font fa{t.r(1.25f), Weight::Regular};
                float aw = text::width(ag, fa);
                text::draw(d, ag, right, y + t.r(1.4f), {fa.size, fa.weight, white(.55f), Align::Right, VAlign::Top});
                float tx = x + ic + gap;
                drawText(d, tx, right - aw - gap - tx, y, row.h, n.src.empty() ? "AnyPS5" : n.src, n.text, t.r(1.85f), false);
            }
            y += row.h;
        }
    }

private:
    PanelHost* host_;
    int top_ = 0;
};

class Notifications : public Screen {
public:
    bool overlay() const override { return true; }
    void enter() override { panels_.open(notes(panels_, iconX(2), 0)); }
    void update(double) override {
        panels_.update();
        if (!panels_.on() && app().router.isOpen(id)) app().router.close(id);
    }
    bool action(const Action& a) override {
        if (a.is("ps")) {
            app().router.close(id);
            return false;
        }
        panels_.action(a);
        return true;
    }
    std::vector<Hint> legend() const override { return {{"Select", "ok"}, {"Delete", "square"}, {"Options", "options"}, {"Back", "back"}}; }
    void draw(Draw& d) override { panels_.draw(d); }

private:
    PanelHost panels_;
};

ANYPS5_SCREEN(Notifications, "notifications");

}

std::unique_ptr<Panel> notes(PanelHost& host, float anchorX, int sel) { return std::make_unique<NotesPanel>(host, anchorX, sel); }

}
