#include "screens/settings.h"
#include "screens/settings_draw.h"
#include "screens/settings_a11yfx.h"
#include "core/Pointer.h"
#include <algorithm>

namespace ui::sett {

struct State {
    bool page = false;
    std::string id, title, user;
    int sec = 0;
    bool right = false;
    int row = 0;
    float scroll = 0;
    bool keep = false;
    Rect anchor{0, 0, 0, 0};
};

static State st;

bool onPage(const std::string& cat) { return st.page && st.id == cat && app().router.isOpen("settings"); }
std::string pageId() { return st.page ? st.id : ""; }
Rect anchor() { return st.anchor; }
void setAnchor(Rect r) { st.anchor = r; }
void resetRow() { st.row = 0; st.scroll = 0; }

void openSettings() {
    st.page = false;
    st.row = 0;
    st.scroll = 0;
    st.keep = true;
    if (!app().router.isOpen("settings")) app().open("settings");
    st.keep = false;
}

void openPage(const std::string& id, const std::string& title, const std::string& userUid) {
    for (auto& a : osAreas())
        if (a.cat == id && a.refresh && areaOn(a.id)) a.refresh();
    st.page = true;
    st.id = id;
    st.title = title;
    st.user = userUid;
    st.sec = 0;
    st.right = false;
    st.row = 0;
    st.scroll = 0;
    if (!userUid.empty()) {
        auto secs = catSections(id);
        for (size_t k = 0; k < secs.size(); k++)
            if (secs[k].user == userUid) {
                st.sec = (int)k;
                st.right = true;
                break;
            }
    }
    st.keep = true;
    if (!app().router.isOpen("settings")) app().open("settings");
    st.keep = false;
}

static int firstRow(const Rows& R) {
    for (size_t i = 0; i < R.size(); i++)
        if (R[i].selectable()) return (int)i;
    return 0;
}

static void pickSection(int i) {
    if (st.sec == i && !st.right) return;
    if (st.sec != i) {
        st.sec = i;
        resetRow();
    }
    st.right = false;
}

class SettingsScreen : public Screen {
public:
    void enter() override {
        if (st.keep) return;
        st.page = false;
        st.row = 0;
        st.scroll = 0;
    }

    void leave() override {
        for (auto& a : osAreas())
            if (a.tick && areaOn(a.id)) a.tick(true);
    }

    void update(double dt) override {
        for (auto& a : osAreas())
            if (a.tick && areaOn(a.id)) a.tick(false);
    }

    std::vector<Hint> legend() const override { return {{"Select", "ok"}, {"Back", "back"}}; }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        if (st.page && !st.right) {
            int n = (int)catSections(st.id).size();
            if (k == "up" && st.sec > 0) {
                st.sec--;
                resetRow();
            } else if (k == "down" && st.sec < n - 1) {
                st.sec++;
                resetRow();
            } else if (k == "right" || k == "ok" || k == "enter") {
                st.right = true;
                st.row = firstRow(rows());
            } else if (k == "back" || k == "left") {
                back();
            } else {
                return false;
            }
            return true;
        }
        Rows R = rows();
        normalize(R);
        auto next = [&](int d) {
            int i = st.row + d;
            while (i >= 0 && i < (int)R.size() && !R[i].selectable()) i += d;
            if (i >= 0 && i < (int)R.size()) st.row = i;
        };
        if (k == "up") next(-1);
        else if (k == "down") next(1);
        else if (k == "ok" || k == "enter" || (k == "right" && !st.page)) run(R);
        else if (k == "back" || k == "left") {
            if (st.page) st.right = false;
            else back();
        } else return false;
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        SDL_Texture* bg = d.cached("sett-bg", t.w, t.h, [&](Draw& c) { paintSettingsBg(c, t.W(), t.H()); });
        d.image(bg, {0, 0, t.W(), t.H()}, Fit::Fill);
        text::draw(d, st.page ? st.title : "Settings", t.r(6), t.r(6.45f), {tsz(2.85f), Weight::Light, white(), Align::Left, VAlign::Middle});
        Rows R = rows();
        normalize(R);
        if (st.page) drawSections(d);
        ListGeom g = listGeom(!st.page);
        bool hot = !st.page || st.right;
        drawList(d, R, g, hot ? st.row : -1, st.scroll, st.anchor, [this](int i, bool click) {
            st.right = true;
            st.row = i;
            if (click) action({"ok", ""});
        }, [this](float rows) { wheel(rows); });
    }

private:
    float wheelAcc_ = 0;

    void wheel(float rows) {
        if (st.page && !st.right) {
            st.right = true;
            st.row = firstRow(this->rows());
        }
        wheelAcc_ += rows;
        while (wheelAcc_ >= 1) {
            wheelAcc_ -= 1;
            action({"down", ""});
        }
        while (wheelAcc_ <= -1) {
            wheelAcc_ += 1;
            action({"up", ""});
        }
    }

    Rows rows() {
        Rows R;
        if (!st.page) {
            for (auto& c : allCats()) {
                Row& r = R.push(c.title, "", [c] { openPage(c.id, c.title); });
                r.icon = catIcon(c.id);
            }
            return R;
        }
        auto secs = catSections(st.id);
        if (st.sec >= (int)secs.size()) st.sec = 0;
        if (secs.empty()) return R;
        Section& s = secs[st.sec];
        if (!s.user.empty()) st.user = s.user;
        else if (st.id == "users") st.user.clear();
        s.rows(R);
        return R;
    }

    void normalize(const Rows& R) {
        if (st.page && st.right) {
            while (st.row < (int)R.size() && !R[st.row].selectable()) st.row++;
            if (st.row >= (int)R.size()) st.row = firstRow(R);
        }
        if (st.row >= (int)R.size()) st.row = std::max(0, (int)R.size() - 1);
    }

    void run(const Rows& R) {
        if (st.row < 0 || st.row >= (int)R.size()) return;
        const Row& r = R[st.row];
        if (!r.selectable() || !r.run) return;
        auto fn = r.run;
        fn();
    }

    void back() {
        if (st.page) {
            std::string id = st.id;
            st.page = false;
            auto cats = allCats();
            st.row = 0;
            st.scroll = 0;
            for (size_t i = 0; i < cats.size(); i++)
                if (cats[i].id == id) st.row = (int)i;
            return;
        }
        app().open(app().me.is_object() ? "home" : "users");
    }

    void drawSections(Draw& d) {
        const Theme& t = app().theme;
        auto secs = catSections(st.id);
        float x = t.r(12.3f) - t.r(1), y = t.r(11.25f), w = t.r(26), h = t.r(6.5f);
        Font f{tsz(1.9f), Weight::Regular};
        for (size_t i = 0; i < secs.size(); i++) {
            Rect r{x, y + h * i, w, h};
            pointer::target(r, [i] { pickSection((int)i); }, [this, i] {
                pickSection((int)i);
                action({"ok", ""});
            }, t.r(.5f));
            bool cur = (int)i == st.sec;
            if (cur && !st.right) {
                d.round(r, t.r(.5f), white(.07f));
                d.outline(r, t.r(.5f), t.r(.1f), white(.85f));
                if (t.highContrast) d.stroke(r, t.r(.5f), t.r(.22f), white());
                setAnchor(r);
                a11yfx::focus(r);
            }
            auto lines = text::wrap(secs[i].name, f, w - t.r(2), 2);
            float lh = f.size * 1.2f;
            float y0 = r.y + r.h / 2 - lh * (lines.size() - 1) / 2;
            for (size_t l = 0; l < lines.size(); l++)
                text::draw(d, lines[l], r.x + t.r(1), y0 + lh * l, {f.size, f.weight, white(cur ? 1.f : .38f), Align::Left, VAlign::Middle});
        }
    }
};

ANYPS5_SCREEN(SettingsScreen, "settings");

}
