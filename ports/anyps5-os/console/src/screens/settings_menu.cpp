#include "core/Anim.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/settings.h"
#include "screens/settings_a11yfx.h"
#include <algorithm>

namespace ui::sett {

static std::vector<MenuItem> pendingItems;
static bool pendingAnchored = false;
static int menuGen = 0;

class SettingsMenu : public Screen {
public:
    bool overlay() const override { return true; }

    void enter() override { take(); }

    void take() {
        items_ = pendingItems;
        anchored_ = pendingAnchored;
        anchor_ = anchor();
        gen_ = menuGen;
        sel_ = 0;
        scroll_ = 0;
        pop_.restart(0, 1, .17f, easeOut);
    }

    void update(double) override {
        if (gen_ != menuGen) take();
    }

    std::vector<Hint> legend() const override { return {{"Select", "ok"}, {"Back", "back"}}; }

    bool action(const Action& a) override {
        if (a.is("up")) sel_ = std::max(0, sel_ - 1);
        else if (a.is("down")) sel_ = std::min((int)items_.size() - 1, sel_ + 1);
        else if (a.is("ok") || a.is("enter")) {
            if (sel_ < 0 || sel_ >= (int)items_.size()) return true;
            auto fn = items_[sel_].fn;
            int g = menuGen;
            app().router.close(id);
            if (fn) fn();
            (void)g;
        } else if (a.is("back")) {
            app().router.close(id);
        }
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        float px = t.rem / 16.f;
        d.rect({0, 0, t.W(), t.H()}, black(.55f));
        pointer::target({0, 0, t.W(), t.H()}, nullptr, [this] { app().router.close(id); });
        float itemH = t.r(4.5f), w = std::min(t.r(34.4f), t.W() - t.r(2));
        std::vector<float> ys;
        float y = t.r(.25f);
        for (auto& it : items_) {
            if (it.sep) y += t.r(.7f);
            ys.push_back(y);
            y += itemH;
        }
        float fullH = y + t.r(.25f);
        float h = std::min(fullH, t.H() - 32 * px);
        Rect r = anchored_ && anchor_.w > 0 ? anchor_ : Rect{t.W() / 2 - 150 * px, t.H() / 2 - 80 * px - 1, 0, 1};
        float left = std::min(t.W() - w - 16 * px, std::max(16 * px, r.x));
        float below = r.y + r.h + 10 * px;
        if (!anchored_ || anchor_.w <= 0) below = t.H() / 2 - 80 * px + 10 * px;
        float top = below + h < t.H() ? below : std::max(16 * px, r.y - h - 10 * px);
        if (sel_ >= 0 && sel_ < (int)ys.size()) {
            float a = ys[sel_], b = ys[sel_] + itemH;
            if (a - scroll_ < 0) scroll_ = a - t.r(.25f);
            if (b - scroll_ > h) scroll_ = b - h + t.r(.25f);
            scroll_ = std::max(0.f, std::min(scroll_, fullH - h));
        }
        float p = pop_.value();
        d.pushAlpha(p);
        Rect box{left, top + (1 - p) * t.r(.6f), w, h};
        d.shadow(box, t.r(.3f), t.r(3), black(.6f), 0, t.r(1));
        d.round(box, t.r(.3f), rgba(40, 42, 50, .97f));
        d.pushClip(box);
        pointer::target(box, nullptr, nullptr);
        pointer::scrollArea(box, [this](float rows) { wheel(rows); });
        Font f{tsz(1.75f), Weight::Regular};
        for (size_t i = 0; i < items_.size(); i++) {
            const MenuItem& it = items_[i];
            Rect ir{box.x, box.y + ys[i] - scroll_, box.w, itemH};
            pointer::target(ir, [this, i] { sel_ = (int)i; }, [this, i] {
                sel_ = (int)i;
                action({"ok", ""});
            }, t.r(.2f));
            if (it.sep) d.rect({ir.x + t.r(1.2f), ir.y - t.r(.4f), ir.w - t.r(2.4f), t.r(.06f)}, white(.15f));
            if ((int)i == sel_) {
                a11yfx::focus(ir);
                d.round(ir, t.r(.2f), white(.07f));
                d.stroke(ir, t.r(.2f), t.r(.1f), white());
                if (t.highContrast) d.stroke(ir, t.r(.2f), t.r(.22f), white());
            }
            float x = ir.x + t.r(2.1f), right = ir.x + ir.w - t.r(2.1f), cy = ir.y + itemH / 2;
            float rw = 0;
            if (!it.right.empty()) {
                rw = text::width(it.right, f) + t.r(2);
                text::draw(d, it.right, right, cy, {f.size, f.weight, white(.6f), Align::Right, VAlign::Middle});
            }
            std::string mark = it.check ? " \xE2\x9C\x93" : "";
            std::string lab = text::ellipsize(it.label, f, right - x - rw - text::width(mark, f)) + mark;
            text::draw(d, lab, x, cy, {f.size, f.weight, white(), Align::Left, VAlign::Middle});
        }
        d.popClip();
        d.popAlpha();
    }

private:
    void wheel(float rows) {
        acc_ += rows;
        while (acc_ >= 1) {
            acc_ -= 1;
            action({"down", ""});
        }
        while (acc_ <= -1) {
            acc_ += 1;
            action({"up", ""});
        }
    }

    float acc_ = 0;
    std::vector<MenuItem> items_;
    bool anchored_ = false;
    Rect anchor_{0, 0, 0, 0};
    int sel_ = 0;
    int gen_ = 0;
    float scroll_ = 0;
    Tween pop_;
};

ANYPS5_SCREEN(SettingsMenu, "settingsmenu");

void menu(std::vector<MenuItem> items, bool anchored) {
    if (items.empty()) return;
    pendingItems = std::move(items);
    pendingAnchored = anchored;
    menuGen++;
    if (!app().router.isOpen("settingsmenu")) app().open("settingsmenu");
}

}
