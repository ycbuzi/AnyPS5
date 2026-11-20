#include "screens/control_center_screen.h"
#include <algorithm>

namespace ui::cc {

namespace {

bool pausedNow() {
    auto it = app().H.find("paused");
    return it != app().H.end() && it->is_boolean() && it->get<bool>();
}

bool showNoArt() { return app().settings.S.value("showNoArt", false); }

void sendHomeIfIngame() {
    if (app().ingame) app().host.send(json{{"home", true}});
}

}

void ControlCenter::closeCc(bool resume) {
    panels_.close();
    app().router.close(id);
    if (resume && app().ingame) app().host.send(json{{"resume", true}});
}

void ControlCenter::toHome() {
    closeCc(false);
    sendHomeIfIngame();
    app().router.go("home");
}

void ControlCenter::openMenu(int icon, const std::string& label, std::vector<MenuItem> items) {
    panels_.open(menu(label, std::move(items), Place::Cc, icon < 0 ? -1 : iconX(icon)));
}

std::vector<Card> ControlCenter::cards() {
    std::vector<Card> C;
    Playing pg;
    bool hasPg = playing(pg);
    bool in = app().ingame;
    if (hasPg) {
        C.push_back({in ? "Now playing" : "Running", pg.title, in ? "Return to the game" : "Select to return to the game", pg.bg.empty() ? pg.icon : pg.bg, pg.icon, "",
                     pg.fps ? std::to_string(pg.fps) + " FPS" : "", "Running", "Resume", [this] { closeCc(false); app().host.send(json{{"resume", true}}); }});
        std::string t = pg.title;
        C.push_back({"Game", "Close game", pg.title + " closes and the pipeline resumes", "", pg.icon, "cc-close", "", "", "Close", [this, t] {
                         openMenu(-1, "", {{"Close " + t, [this] { closeCc(false); app().host.send(json{{"closegame", true}}); }}, {"Cancel", {}}});
                     }});
    }
    const auto& dl = dlItems();
    if (!dl.empty()) {
        auto top = std::find_if(dl.begin(), dl.end(), [](const DlItem& x) { return x.pct >= 0; });
        const DlItem& x = top != dl.end() ? *top : dl[0];
        const host::Game* g = dlGame(x);
        C.push_back({"Downloads" + (dl.size() > 1 ? " (" + std::to_string(dl.size()) + ")" : std::string()), x.title, x.status.empty() ? dlKind(x.kind) : x.status, "",
                     g ? g->icon : "", "cc-glyph-download", x.pct >= 0 ? std::to_string(x.pct) + "%" : "", dlKind(x.kind), "Open", [this] {
                         crow_ = 1;
                         qsel_ = 3;
                         panels_.open(downloads(iconX(3), 0));
                     }});
    }
    bool p = pausedNow();
    if (app().H.value("platform", std::string()) == "windows") C.push_back({"Test pipeline", p ? "Paused" : "Running", p ? "Select to resume it" : "Select to pause it", "", "", "cc-pipeline", p ? "Paused" : "On",
                 p ? "Waiting" : "Testing", p ? "Resume" : "Pause", [p] { app().host.send(json{{"pause", !p}}); }});
    auto lib = app().library.visible(showNoArt());
    int plays = (int)std::count_if(lib.begin(), lib.end(), [](const host::Game& g) { return g.status == "Plays"; });
    C.push_back({"Your games", "Games that play", std::to_string(lib.size()) + " games in the library", "", "", "cc-grid",
                 std::to_string(lib.empty() ? 0 : (int)(plays * 100.0 / lib.size() + .5)) + "%", std::to_string(plays) + " play", "Open", [this] {
                     closeCc(false);
                     sendHomeIfIngame();
                     app().open("library");
                 }});
    const host::Game* shot = nullptr;
    for (auto& g : app().library.all)
        if (!g.shot.empty() && (!shot || g.lastRun > shot->lastRun)) shot = &g;
    if (shot)
        C.push_back({"Captures", shot->title, "Latest screenshot", shot->shot, shot->icon, "", "", "", "View", [this] {
                         closeCc(false);
                         sendHomeIfIngame();
                         app().open("gallery");
                     }});
    auto& notes = app().toasts.history();
    C.push_back({"Notifications", notes.empty() ? "No notifications" : notes[0].text, notes.empty() ? "" : age(notes[0].wall), "", "", "cc-glyph-bell", "", "", "View",
                 [this] { crow_ = 1; qsel_ = 2; panels_.open(cc::notes(panels_, iconX(2), 0)); }});
    return C;
}

std::vector<CcIcon> ControlCenter::icons() {
    Playing pg;
    bool hasPg = playing(pg);
    bool p = pausedNow();
    std::vector<CcIcon> I;
    I.push_back({"Home", "cc-home", IconKind::Svg, false, [this] { toHome(); }});
    I.push_back({"Switcher", "cc-switcher", hasPg && !pg.icon.empty() ? IconKind::Switcher : IconKind::Svg, false, [this, hasPg, pg] {
                     std::vector<MenuItem> m;
                     if (hasPg) {
                         m.push_back({"Back to " + pg.title, [this] { closeCc(false); app().host.send(json{{"resume", true}}); }});
                         m.push_back({"Close " + pg.title, [this] { closeCc(false); app().host.send(json{{"closegame", true}}); }});
                     }
                     int n = 0;
                     for (auto& g : app().library.recent(showNoArt())) {
                         if (g.lastRun.empty() || n >= 6) break;
                         n++;
                         if (hasPg && g.id == pg.id) continue;
                         host::Game copy = g;
                         m.push_back({g.title, [this, copy] { closeCc(false); app().play(copy); }});
                     }
                     m.push_back({"Cancel", {}});
                     openMenu(1, "Switcher", m);
                 }});
    I.push_back({"Notifications", "cc-bell", IconKind::Svg, false, [this] { panels_.open(notes(panels_, iconX(2), 0)); }});
    I.push_back({"Downloads", "cc-download", IconKind::Svg, !dlItems().empty(), [this] { panels_.open(downloads(iconX(3), 0)); }});
    I.push_back({"Screenshot", "cc-camera", IconKind::Svg, false, [this] { closeCc(false); app().host.send(json{{"shot", true}}); }});
    I.push_back({p ? "Pipeline paused" : "Pipeline running", p ? "cc-play" : "cc-pause", IconKind::Svg, p, [p] { app().host.send(json{{"pause", !p}}); }});
    I.push_back({"Sound", "cc-sound", IconKind::Svg, false, [this] {
                     auto& S = app().settings.S;
                     bool on = S.value("sounds", false);
                     auto vol = [](int v) { return [v] { app().settings.S["volume"] = v; app().saveSettings(); }; };
                     openMenu(6, "Sound", {{on ? "Turn menu sounds off" : "Turn menu sounds on", [on] { app().settings.S["sounds"] = !on; app().saveSettings(); }},
                                           {"Volume: Low", vol(1)}, {"Volume: Medium", vol(2)}, {"Volume: High", vol(3)}, {"Cancel", {}}});
                 }});
    I.push_back({"Accessories", "cc-pad", IconKind::Svg, false, [this] {
                     std::vector<MenuItem> m;
                     auto it = app().H.find("pads");
                     if (it != app().H.end() && it->is_array() && !it->empty()) {
                         for (auto& n : *it)
                             if (n.is_string()) m.push_back({n.get<std::string>(), {}});
                     } else {
                         m.push_back({"No controller found", {}});
                     }
                     m.push_back({"Controller settings", [this] { closeCc(false); sendHomeIfIngame(); app().open("settings"); }});
                     openMenu(7, "Accessories", m);
                 }});
    std::string name = app().me.is_object() ? app().me.value("name", std::string("Profile")) : "Profile";
    I.push_back({name, "", IconKind::Avatar, false, [this, name] {
                     openMenu(8, name, {{"Trophies", [this] { closeCc(false); sendHomeIfIngame(); app().open("trophies"); }},
                                        {"Switch user", [this] {
                                             closeCc(false);
                                             sendHomeIfIngame();
                                             if (dndOn()) setDnd(false);
                                             app().me = json();
                                             app().router.go("users");
                                         }},
                                        {"Settings", [this] { closeCc(false); sendHomeIfIngame(); app().open("settings"); }},
                                        {"Controls and help", [this] { closeCc(false); app().open("help"); }},
                                        {"Cancel", {}}});
                 }});
    I.push_back({"Power", "cc-power", IconKind::Svg, false, [this] { panels_.open(power(iconX(9))); }});
    return I;
}

}
