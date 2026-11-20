#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/library.h"
#include <algorithm>

namespace ui {

class PsMenu : public Screen {
public:
    bool overlay() const override { return true; }

    void enter() override {
        sel_ = 0;
        in_.restart(0, 1, .17f, easeOut);
    }

    void set(std::vector<MenuItem> items, Rect anchor, bool beside, bool animate) {
        items_ = std::move(items);
        anchor_ = anchor;
        beside_ = beside;
        sel_ = 0;
        if (animate) in_.restart(0, 1, .17f, easeOut);
    }

    void swap(std::vector<MenuItem> items) {
        items_ = std::move(items);
        sel_ = 0;
        in_.restart(0, 1, .17f, easeOut);
    }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        int n = (int)items_.size();
        if (k == "up" && sel_ > 0) sel_--;
        else if (k == "down" && sel_ < n - 1) sel_++;
        else if (k == "ok" || k == "enter") pick();
        else if (k == "back" || k == "ps") app().router.pop();
        return true;
    }

    std::vector<Hint> legend() const override { return {{"Select", "ok"}, {"Back", "back"}}; }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, black(.55f));
        pointer::target({0, 0, t.W(), t.H()}, nullptr, [] { app().router.pop(); });
        if (items_.empty()) return;
        Rect box = place();
        float k = in_.value();
        box.y += t.r(.7f) * (1 - k);
        d.pushAlpha(k);
        float rad = t.r(.3f);
        d.shadow(box, rad, t.r(3), black(.6f), 0, t.r(1));
        d.round(box, rad, rgba(40, 42, 50, .97f));
        pointer::target(box, nullptr, [] {}, rad);
        float y = box.y + t.r(.25f);
        TextStyle st{t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Middle};
        for (int i = 0; i < (int)items_.size(); i++) {
            const MenuItem& it = items_[i];
            if (it.sep) {
                y += t.r(.7f);
                d.rect({box.x + t.r(1.2f), y - t.r(.4f), box.w - t.r(2.4f), std::max(1.f, t.r(.06f))}, white(.15f));
            }
            Rect row{box.x, y, box.w, t.r(4.5f)};
            if (i == sel_) {
                d.round(row, t.r(.2f), white(.07f));
                d.stroke(row, t.r(.2f), std::max(1.f, t.r(.1f)), white());
            }
            text::draw(d, text::ellipsize(it.label, st.font(), row.w - t.r(4.2f)), row.x + t.r(2.1f), row.y + row.h / 2, st);
            pointer::target(row, [this, i] { sel_ = i; }, [this, i] {
                sel_ = i;
                pick();
            }, t.r(.2f));
            y += row.h;
        }
        d.popAlpha();
    }

private:
    Rect place() const {
        const Theme& t = app().theme;
        float w = std::min(t.r(34.4f), t.W() - t.r(2)), h = t.r(.5f), rem = t.r(1);
        for (auto& it : items_) h += t.r(4.5f) + (it.sep ? t.r(.7f) : 0);
        Rect a = anchor_;
        if (beside_) {
            float side = a.x + a.w + t.r(.4f);
            if (side + w < t.W() - rem) return {side, std::clamp(a.y - t.r(.2f), rem, std::max(rem, t.H() - h - rem)), w, h};
            return {std::clamp(a.x, rem, std::max(rem, t.W() - w - rem)), std::min(t.H() - h - rem, a.y + a.h + t.r(.4f)), w, h};
        }
        float left = std::min(t.W() - w - rem, std::max(rem, a.x));
        float below = a.y + a.h + t.r(.625f);
        return {left, below + h < t.H() ? below : std::max(rem, a.y - h - t.r(.625f)), w, h};
    }

    void pick() {
        if (sel_ < 0 || sel_ >= (int)items_.size()) return;
        MenuItem it = items_[sel_];
        if (!it.keep) app().router.pop();
        if (it.run) it.run();
    }

    std::vector<MenuItem> items_;
    Rect anchor_{};
    bool beside_ = true;
    int sel_ = 0;
    Tween in_;
};

ANYPS5_SCREEN(PsMenu, "psmenu");

class GameMenu : public PsMenu {
public:
    void enter() override {
        PsMenu::enter();
        App& A = app();
        if (!A.focusGame.is_object() || A.focusGame.empty()) {
            set({}, {}, true, false);
            A.router.pop();
            return;
        }
        host::Game g = host::Game::from(A.focusGame);
        bool running = A.focusGame.value("running", false);
        LibEntry e = libraryEntry(g);
        set({{running ? "Close Game" : "Play Game", [g, running] { running ? app().host.send(json{{"closegame", true}}) : app().play(g); }},
             {"Information", [e] { gameinfo::show(e); }, true},
             {"Game Library", [] { app().open("library"); }},
             {"Your Collection", [] { openCollection("home"); }},
             {"Check for New Games", [] { app().host.send(json{{"rescan", true}}); }, true}},
            psmenu::homeTile(), true, false);
    }
};

ANYPS5_SCREEN(GameMenu, "gamemenu");

namespace psmenu {

namespace {
PsMenu* top() {
    Screen* s = app().router.top();
    return s && s->id == "psmenu" ? dynamic_cast<PsMenu*>(s) : nullptr;
}

bool isRunning(const LibEntry& e) {
    if (e.g.raw.is_object() && e.g.raw.value("running", false)) return true;
    const host::Game* pg = app().playingGame();
    return pg && !e.g.id.empty() && pg->id == e.g.id;
}
}

void show(std::vector<MenuItem> items, Rect anchor, bool beside) {
    if (auto* m = dynamic_cast<PsMenu*>(app().open("psmenu"))) m->set(std::move(items), anchor, beside, true);
}

void swapItems(std::vector<MenuItem> items) {
    Screen* s = app().router.top();
    if (auto* m = dynamic_cast<PsMenu*>(s)) m->swap(std::move(items));
}

void replace(std::vector<MenuItem> items, Rect anchor, bool beside) {
    if (PsMenu* m = top()) m->set(std::move(items), anchor, beside, true);
    else show(std::move(items), anchor, beside);
}

std::vector<MenuItem> forEntry(const LibEntry& e, bool fromLibrary) {
    using collection::State;
    std::vector<MenuItem> out;
    std::string id = e.g.id, title = e.g.title;
    bool can = collection::supported() && !id.empty();
    const collection::Item* ci = can ? collection::byId(id) : nullptr;
    std::string verb = ci ? collection::actionVerb(*ci) : "";
    bool enabled = ci && (ci->actionId.empty() || ci->actionEnabled);
    if (e.playable()) {
        bool running = isRunning(e);
        host::Game g = e.g;
        out.push_back({running ? "Close Game" : "Play Game", [g, running] { running ? app().host.send(json{{"closegame", true}}) : app().play(g); }});
    } else if (e.st == State::Installing) {
        out.push_back({collection::labelFor(id), [title, id] { app().toast(title + " is installing (" + collection::labelFor(id) + ")"); }});
    } else if (can && !ci && e.st != State::Installed) {
        out.push_back({"Install", [id] { collection::install(id); }});
    }
    if (enabled && (verb == "Install" || verb == "Update" || verb == "Retry") && e.st != State::Installing) out.push_back({verb, [id] { collection::act(id); }});
    if (ci && e.st == State::Installing) out.push_back({"Cancel Install", [id] { collection::cancel(id); }});
    out.push_back({"Information", [e] { gameinfo::show(e); }, !out.empty()});
    bool built = e.st == State::Installed || (ci && ci->installed) || (e.st != State::NotInstalled && e.st != State::Unsupported && !e.g.exe.empty());
    if (can && e.st != State::Installing && built) {
        if (!ci || enabled || verb != "Reinstall") out.push_back({"Reinstall", [id] { collection::reinstall(id); }, true});
        out.push_back({"Delete", [id, title] {
                           swapItems({{"Delete " + title, [id] { collection::remove(id); }}, {"Cancel", [] {}, true}});
                       },
                       false, true});
    }
    if (fromLibrary) out.push_back({"Sort and Filter", [] { replace(sortItems(), Library::sortButton(), false); }, true, true});
    else out.push_back({"Game Library", [] { app().open("library"); }, true});
    if (app().router.baseId() != "collection") out.push_back({"Your Collection", [] { openCollection(app().router.baseId()); }});
    return out;
}

Rect homeTile() {
    constexpr float kSmall = 6.5f, kBig = 10.94f, kGap = .65f, kRowLeft = 10.5f;
    App& A = app();
    const Theme& t = A.theme;
    auto view = A.library.recent(A.settings.get("showNoArt", false) == true);
    if (view.size() > 12) view.resize(12);
    std::vector<std::pair<std::string, std::string>> row;
    const host::Game* pg = A.playingGame();
    std::string playing = A.playing();
    if (!playing.empty()) row.push_back({pg ? pg->id : "", pg ? pg->title : playing});
    for (auto& g : view)
        if (!(pg && !g.id.empty() && g.id == pg->id)) row.push_back({g.id, g.title});
    int sel = 0;
    std::string id = A.focusGame.is_object() ? A.focusGame.value("id", std::string()) : "";
    bool running = A.focusGame.is_object() && A.focusGame.value("running", false);
    if (!running && !id.empty())
        for (size_t i = 0; i < row.size(); i++)
            if (row[i].first == id) {
                sel = (int)i;
                break;
            }
    std::string title = sel < (int)row.size() ? row[sel].second : "";
    float nameW = text::width(title, {t.r(1.8f), Weight::Regular});
    float left = sel * t.r(kSmall + kGap), right = left + t.r(kBig);
    float shift = std::max(0.f, right + t.r(.75f) + nameW - (t.W() - t.r(kRowLeft) - t.r(3)));
    return {t.r(kRowLeft) + left - shift, t.r(7.8f), t.r(kBig), t.r(kBig)};
}

std::vector<MenuItem> sortItems() {
    static const char* sorts[][2] = {{"recent", "Most Recent"}, {"az", "Alphabetical"}, {"status", "Plays First"}};
    std::string curKey = app().settings.get("libSort", "recent").get<std::string>();
    std::vector<MenuItem> out;
    for (auto& s : sorts) {
        std::string key = s[0];
        out.push_back({(curKey == key ? "\xE2\x9C\x93  Sort by: " : "Sort by: ") + std::string(s[1]), [key] {
                           App& A = app();
                           A.settings.S["libSort"] = key;
                           A.saveSettings();
                           if (auto* lib = dynamic_cast<Library*>(A.router.get("library"))) lib->sorted();
                       }});
    }
    out.push_back({"Cancel", [] {}, true});
    return out;
}

}

}
