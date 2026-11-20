#include "core/App.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "widgets/Widgets.h"

namespace ui {

class Users : public Screen {
public:
    void enter() override {
        rebuild();
        if (sel_ < 1 && list_.size() > 1) sel_ = 1;
        snap();
    }

    void host(const json& m) override {
        if (m.value("host", false) || m.contains("settings")) {
            rebuild();
            snapTargets();
        }
    }

    std::vector<Hint> legend() const override { return {{"Select", "ok"}, {"Help", "create"}}; }

    bool action(const Action& a) override {
        if (a.is("left") && sel_ > 0) sel_--;
        else if (a.is("right") && sel_ + 1 < (int)list_.size()) sel_++;
        else if (a.is("ok") || a.is("enter")) {
            pick();
            return true;
        } else if (a.is("create") || a.is("help")) {
            app().open("help");
            return true;
        } else {
            return a.is("ps") || a.is("back");
        }
        snapTargets();
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, hex(0x0a0e17));
        widgets::clock(d, t.W() - t.r(4.4f), t.r(3.75f), t.r(2.1f));
        text::draw(d, "Welcome back to AnyPS5", t.W() / 2, t.r(13.45f), {t.r(4), Weight::Light, white(), Align::Center, VAlign::Middle});
        text::draw(d, "Who's using this controller?", t.W() / 2, t.r(19.4f), {t.r(1.85f), Weight::Light, white(.85f), Align::Center, VAlign::Middle});
        size_t n = list_.size();
        float itemW = t.r(16.25f), gap = t.r(4.6f);
        float total = n * itemW + (n ? (n - 1) * gap : 0);
        float x0 = t.W() / 2 - total / 2, top = t.r(31);
        float small = t.r(11.9f), big = t.r(16.25f);
        for (size_t i = 0; i < n; i++) {
            const json& p = list_[i];
            bool add = p.value("add", false);
            float cx = x0 + i * (itemW + gap) + itemW / 2;
            float cy = top + t.r(8.125f);
            float dia = small + (big - small) * size_[i].value();
            float on = size_[i].value();
            pointer::target({cx - itemW / 2, top, itemW, t.r(16.25f) + t.r(6)}, [this, i] { hover((int)i); }, [this, i] { hover((int)i); pick(); }, t.r(1));
            if (on > 0) d.outline({cx - dia / 2, cy - dia / 2, dia, dia}, dia / 2, t.r(.25f), white(on));
            if (add) {
                d.circle(cx, cy, dia / 2, hex(0x4a4c55));
                text::draw(d, "+", cx, cy, {t.r(4), Weight::Light, white(), Align::Center, VAlign::Middle});
            } else {
                widgets::avatar(d, cx, cy, dia, p, t.r(4.4f));
            }
            bool selected = (int)i == sel_;
            TextStyle name{t.r(1.75f), Weight::Regular, white(), Align::Center, VAlign::Middle};
            float nameH = text::lineHeight(name.font());
            float ny = top + t.r(16.25f) + t.r(1.9f) + nameH / 2;
            std::string label = p.value("name", std::string());
            bool locked = !p.value("pinHash", std::string()).empty();
            float lw = text::width(label, name.font());
            float lockW = locked ? t.r(1.5f) : 0;
            text::draw(d, label, cx - lockW / 2, ny, name);
            if (locked) d.icon(images::icon("lock", t.r(1.4f)), cx - lockW / 2 + lw / 2 + t.r(.95f), ny, t.r(1.4f));
            if (selected && !add) {
                TextStyle em{t.r(1.25f), Weight::Regular, white(.75f), Align::Center, VAlign::Middle};
                text::draw(d, "Options", cx, ny + nameH / 2 + t.r(.4f) + text::lineHeight(em.font()) / 2, em);
                float gy = top - t.r(6.3f);
                text::draw(d, "1", cx, gy + t.r(.65f), {t.r(1.3f), Weight::Regular, white(), Align::Center, VAlign::Middle});
                d.icon(images::icon("pad", t.r(3.3f)), cx, gy + t.r(1.4f) + t.r(1.65f), t.r(3.3f));
            }
        }
    }

private:
    void hover(int i) {
        if (i == sel_ || i < 0 || i >= (int)list_.size()) return;
        sel_ = i;
        snapTargets();
    }

    void rebuild() {
        list_ = json::array({json{{"name", "Add User"}, {"add", true}}});
        for (auto& p : app().settings.profiles()) list_.push_back(p);
        if (sel_ >= (int)list_.size()) sel_ = (int)list_.size() - 1;
        size_.resize(list_.size());
    }

    void snap() {
        size_.assign(list_.size(), Tween{});
        for (size_t i = 0; i < list_.size(); i++) size_[i].snap((int)i == sel_ ? 1.f : 0.f);
    }

    void snapTargets() {
        size_.resize(list_.size());
        for (size_t i = 0; i < list_.size(); i++) size_[i].set((int)i == sel_ ? 1.f : 0.f, .25f, easeCss);
    }

    void pick() {
        App& A = app();
        const json& p = list_[(size_t)sel_];
        if (p.value("add", false)) {
            if (!A.open("adduser")) A.toast("Adding a user is not available yet");
            return;
        }
        if (!p.value("pinHash", std::string()).empty()) {
            A.login = p;
            if (!A.open("passcode")) A.toast("The passcode page is not available yet");
            return;
        }
        A.enterHome(p);
    }

    json list_ = json::array();
    std::vector<Tween> size_;
    int sel_ = 1;
};

ANYPS5_SCREEN(Users, "users");

}
