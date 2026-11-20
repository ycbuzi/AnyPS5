#include "screens/control_center.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::cc {

namespace {

class CcMenu : public Panel {
public:
    explicit CcMenu(std::vector<MenuItem> items) : items_(std::move(items)) {}
    int count() const override { return (int)items_.size(); }
    Color background() const override { return rgba(20, 24, 40, .98f); }
    float radius() const override { return app().theme.r(1.2f); }
    float headH() const {
        const Theme& t = app().theme;
        return head.empty() ? 0 : t.r(1.6f) + text::lineHeight({t.r(1.6f), Weight::Semibold});
    }
    float rowH() const {
        const Theme& t = app().theme;
        return t.r(1.9f) + text::lineHeight({t.r(1.4f), Weight::Regular});
    }
    float width() const override {
        const Theme& t = app().theme;
        float w = head.empty() ? 0 : text::width(head, {t.r(1.6f), Weight::Semibold});
        for (auto& i : items_) w = std::max(w, text::width(i.label, {t.r(1.4f), Weight::Regular}));
        return std::max(t.r(30), w + t.r(2.6f) + t.r(2));
    }
    float height() const override { return app().theme.r(2) + headH() + rowH() * items_.size(); }
    Run pick() override { return sel < count() ? items_[sel].run : Run(); }
    void draw(Draw& d, Rect r) override {
        const Theme& t = app().theme;
        float x = r.x + t.r(1), y = r.y + t.r(1), w = r.w - t.r(2);
        if (!head.empty()) {
            text::draw(d, head, x + t.r(1.3f), y + t.r(.6f), {t.r(1.6f), Weight::Semibold, white(), Align::Left, VAlign::Top});
            y += headH();
        }
        float h = rowH();
        for (int i = 0; i < count(); i++) {
            Rect row{x, y, w, h};
            pointer::target(row, [this, i] { hoverRow(i); }, [this, i] { hoverRow(i); app().act({"ok", ""}); }, t.r(.5f));
            if (i == sel) {
                d.round(row, t.r(.5f), white(.14f));
                d.outline(row, t.r(.5f), t.r(.15f), white());
            }
            text::draw(d, text::ellipsize(items_[i].label, {t.r(1.4f), Weight::Regular}, w - t.r(2.6f)), x + t.r(1.3f), y + h / 2,
                       {t.r(1.4f), Weight::Regular, white(), Align::Left, VAlign::Middle});
            y += h;
        }
    }

private:
    std::vector<MenuItem> items_;
};

class PsMenu : public Panel {
public:
    explicit PsMenu(std::vector<MenuItem> items) : items_(std::move(items)) {}
    int count() const override { return (int)items_.size(); }
    Color background() const override { return rgba(40, 42, 50, .97f); }
    float width() const override { return std::min(app().theme.r(34.4f), app().theme.W() - app().theme.r(2)); }
    float height() const override {
        const Theme& t = app().theme;
        float h = t.r(.5f);
        for (auto& i : items_) h += t.r(4.5f) + (i.sep ? t.r(.7f) : 0);
        return h;
    }
    Run pick() override { return sel < count() ? items_[sel].run : Run(); }
    void draw(Draw& d, Rect r) override {
        const Theme& t = app().theme;
        float y = r.y + t.r(.25f);
        Font f{t.r(1.75f), Weight::Regular};
        for (int i = 0; i < count(); i++) {
            if (items_[i].sep) {
                y += t.r(.7f);
                d.rect({r.x + t.r(1.2f), y - t.r(.4f), r.w - t.r(2.4f), t.r(.06f)}, white(.15f));
            }
            Rect row{r.x, y, r.w, t.r(4.5f)};
            pointer::target(row, [this, i] { hoverRow(i); }, [this, i] { hoverRow(i); app().act({"ok", ""}); });
            if (i == sel) drawRowSel(d, row);
            text::draw(d, text::ellipsize(items_[i].label, f, r.w - t.r(4.2f)), r.x + t.r(2.1f), y + row.h / 2,
                       {f.size, f.weight, white(), Align::Left, VAlign::Middle});
            y += row.h;
        }
    }

private:
    std::vector<MenuItem> items_;
};

}

std::unique_ptr<Panel> menu(const std::string& head, std::vector<MenuItem> items, Place place, float anchorX, Rect anchor) {
    auto p = std::make_unique<CcMenu>(std::move(items));
    p->head = head;
    p->place = place;
    p->anchorX = anchorX;
    p->anchor = anchor;
    return p;
}

std::unique_ptr<Panel> psMenu(std::vector<MenuItem> items, Rect anchor) {
    auto p = std::make_unique<PsMenu>(std::move(items));
    p->place = Place::Above;
    p->anchor = anchor;
    return p;
}

}
