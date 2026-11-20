#include "screens/control_center.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::cc {

namespace {

struct PowerRow {
    std::string icon, title, desc;
    Run run;
};

class PowerPanel : public Panel {
public:
    explicit PowerPanel(float ax) {
        head = "Power";
        anchorX = ax;
        Playing pg;
        std::string g = playing(pg) ? pg.title : "";
        rows_ = {
            {"pw-rest", "Enter Rest Mode",
             g.empty() ? "Darkens the screen. Press any button to wake AnyPS5. The test pipeline keeps working."
                       : "Pauses " + g + " where it is and darkens the screen. Press any button to wake AnyPS5 and carry on playing.",
             [] { enterRest(); }},
            {"pw-off", "Turn Off AnyPS5", (g.empty() ? "Closes " : "Closes " + g + " and ") + "AnyPS5. The test pipeline keeps working.",
             [] { powerOff(false); }},
            {"pw-restart", "Restart AnyPS5", (g.empty() ? "Starts" : "Closes " + g + " and starts") + std::string(" AnyPS5 again."),
             [] { powerOff(true); }}};
    }
    int count() const override { return (int)rows_.size(); }
    float width() const override { return std::min(app().theme.r(41), app().theme.W() - app().theme.r(4)); }
    float headH() const {
        const Theme& t = app().theme;
        return t.r(2.5f) + text::lineHeight({t.r(1.5f), Weight::Regular});
    }
    float textX() const { return app().theme.r(1.6f + 2.3f + 1.4f); }
    std::vector<std::string> descLines(int i) const {
        const Theme& t = app().theme;
        return text::wrap(rows_[i].desc, {t.r(1.3f), Weight::Regular}, width() - textX() - t.r(1.6f));
    }
    float rowH(int i) const {
        const Theme& t = app().theme;
        float h = t.r(3.4f) + text::lineHeight({t.r(1.75f), Weight::Regular});
        if (i == sel) h += t.r(.5f) + descLines(i).size() * t.r(1.3f * 1.4f);
        return h;
    }
    float height() const override {
        float h = headH() + app().theme.r(.6f);
        for (int i = 0; i < count(); i++) h += rowH(i);
        return h;
    }
    Run pick() override { return rows_[sel].run; }
    void draw(Draw& d, Rect r) override {
        const Theme& t = app().theme;
        text::draw(d, head, r.x + t.r(1.6f), r.y + t.r(1.5f), {t.r(1.5f), Weight::Regular, white(.75f), Align::Left, VAlign::Top});
        float y = r.y + headH();
        Font ft{t.r(1.75f), Weight::Regular};
        for (int i = 0; i < count(); i++) {
            Rect row{r.x, y, r.w, rowH(i)};
            if (i < count() - 1) d.rect({r.x, y + row.h - t.r(.06f), r.w, t.r(.06f)}, white(.08f));
            if (i == sel) drawRowSel(d, row);
            pointer::target(row, [this, i] { hoverRow(i); }, [this, i] { hoverRow(i); app().act({"ok", ""}); });
            float is = t.r(2.3f);
            SDL_Texture* ic = images::icon(rows_[i].icon, is);
            if (ic) d.icon(ic, r.x + t.r(1.6f) + is / 2, y + t.r(1.8f) + is / 2, is);
            float tx = r.x + textX(), ty = y + t.r(1.7f);
            text::draw(d, text::ellipsize(rows_[i].title, ft, r.w - textX() - t.r(1.6f)), tx, ty, {ft.size, ft.weight, white(), Align::Left, VAlign::Top});
            if (i == sel) {
                float lh = t.r(1.3f * 1.4f);
                Font fd{t.r(1.3f), Weight::Regular};
                float off = (lh - text::lineHeight(fd)) / 2;
                text::drawLines(d, descLines(i), tx, ty + text::lineHeight(ft) + t.r(.5f) + off, lh, {fd.size, fd.weight, white(.65f), Align::Left, VAlign::Top});
            }
            y += row.h;
        }
    }

private:
    std::vector<PowerRow> rows_;
};

}

std::unique_ptr<Panel> power(float anchorX) { return std::make_unique<PowerPanel>(anchorX); }

}
