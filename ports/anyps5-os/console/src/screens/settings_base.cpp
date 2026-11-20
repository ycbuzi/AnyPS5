#include "screens/settings.h"
#include "core/Glyphs.h"
#include "host/Library.h"
#include <cmath>
#include <cstdio>

namespace ui::sett {

static std::string gbText(double b) {
    char buf[32];
    if (b >= 1e12) std::snprintf(buf, sizeof buf, "%.2f TB", b / 1e12);
    else std::snprintf(buf, sizeof buf, "%d GB", (int)std::lround(b / 1e9));
    return buf;
}

static json opt(const char* group, const std::string& k, const json& d) {
    const json& S = app().settings.S;
    if (!S.contains(group) || !S[group].is_object() || !S[group].contains(k)) return d;
    return S[group][k];
}

static void setOpt(const char* group, const std::string& k, const json& v) {
    json& S = app().settings.S;
    if (!S.contains(group) || !S[group].is_object()) S[group] = json::object();
    S[group][k] = v;
    app().saveSettings();
}

static bool sb(const std::string& k, bool d = false) {
    json v = app().settings.get(k, d);
    return v.is_boolean() ? v.get<bool>() : d;
}

static void setS(const std::string& k, const json& v, bool apply = false) {
    app().settings.S[k] = v;
    if (apply) saveApply();
    else app().saveSettings();
}

static std::string rootText() {
    std::string r = jstr(app().H, "root");
    return r.empty() ? "D:\\AnyPS5 Games" : r;
}

static int trophyGames = 0;
static bool trophyAsked = false;

static void loadTrophies() {
    if (trophyAsked) return;
    trophyAsked = true;
    app().host.get("/cache/trophies.js", [](int status, const std::string& body) {
        if (status != 200) return;
        json t = host::parseJsAssignment(body);
        if (t.is_object() && t.contains("games") && t["games"].is_array()) trophyGames = (int)t["games"].size();
    });
}

static std::vector<Section> storage() {
    return {{"Drives", "", [](Rows& R) {
                const json& d = app().H.contains("drives") ? app().H["drives"] : json::array();
                if (!d.is_array() || d.empty()) R.info("Drives", "Shown in AnyPS5.exe");
                if (d.is_array())
                    for (auto& x : d) {
                        std::string n = jstr(x, "name"), l = jstr(x, "label");
                        double fr = x.value("free", 0.0), tot = x.value("total", 0.0);
                        R.info(n + (l.empty() ? "" : " " + l), gbText(fr) + " free of " + gbText(tot));
                    }
                R.info("Games Folder", rootText()).secret = true;
                R.note("Games stay in the games folder; AnyPS5's own previews and lists live in _tools\\console\\cache.");
            }},
        {"Games and Media", "", [](Rows& R) {
             loadTrophies();
             R.info("Games", std::to_string(app().library.all.size()));
             const json& m = app().media;
             R.info("Captures", std::to_string(m.contains("items") && m["items"].is_array() ? m["items"].size() : 0));
             R.info("Games With Trophy Lists", std::to_string(trophyGames));
             R.push("Open the Media Gallery", "", [] { app().open("gallery"); });
         }}};
}

static std::vector<Section> capture() {
    auto cap = [](const std::string& k, const json& d) { return opt("capture", k, d); };
    return {{"Captures", "", [cap](Rows& R) {
                std::string fmt = cap("fmt", "png").is_string() ? cap("fmt", "png").get<std::string>() : "png";
                R.push("Screenshot File Type", fmt == "jpg" ? "JPG" : "PNG", [fmt] { setOpt("capture", "fmt", fmt == "png" ? "jpg" : "png"); });
                R.tog("Save Trophy Screenshots", [cap] { return cap("trophy", true) != false; }, [](bool v) { setOpt("capture", "trophy", v); });
                R.tog("Show a Notification When Saved", [cap] { return cap("notify", true) != false; }, [](bool v) { setOpt("capture", "notify", v); });
                R.note("Screenshots go to each game's screenshots folder and show up in the Media Gallery.");
            }},
        {"Shortcuts for Create Button", "", [cap](Rows& R) {
             bool shot = cap("createMode", "menu") == "shot";
             R.push("Create Button", shot ? "Easy Screenshots" : "Standard", [shot] { setOpt("capture", "createMode", shot ? "menu" : "shot"); });
             R.note(glyph::fill("Standard: press {create} for the Create menu. Easy Screenshots: press {create} to take a screenshot, hold it for the Create menu. On the keyboard C takes the screenshot.", glyph::pad()));
         }}};
}

static std::vector<Section> notes() {
    auto note = [](const std::string& k, const json& d) { return app().settings.noteOpt(k, d); };
    auto setNote = [](const std::string& k, const json& v) {
        app().settings.setNoteOpt(k, v);
        saveApply();
    };
    return {{"Notifications", "", [note, setNote](Rows& R) {
                R.tog("Allow Pop-Up Notifications", [note] { return note("popups", true) != false; }, [setNote](bool v) { setNote("popups", v); });
                json tj = note("time", 3);
                int t = tj.is_number() ? tj.get<int>() : 3;
                R.push("Display Time", std::to_string(t) + " seconds", [t, setNote] { setNote("time", t == 3 ? 5 : t == 5 ? 10 : 3); });
                R.tog("Do Not Disturb", [] { return dnd(); }, [](bool v) { setDnd(v); });
                R.note("Do Not Disturb stays on until you log out. Notifications still go into the list.");
            }},
        {"Trophies", "", [note, setNote](Rows& R) {
             R.tog("Trophy Pop-Ups Over Games", [note] { return note("trophyPopup", true) != false; }, [setNote](bool v) { setNote("trophyPopup", v); });
             R.note("The trophy sound is under Sound.");
         }},
        {"Notification List", "", [](Rows& R) {
             R.info("Notifications", std::to_string(app().toasts.history().size()));
             R.push("View Notifications", "", [] { app().open("notifications"); });
             R.push("Delete All Notifications", "", [] { app().toasts.history().clear(); });
         }}};
}

static int setI(const char* k, int d, int hi) {
    json v = app().settings.get(k, d);
    int n = v.is_number_integer() ? v.get<int>() : d;
    return n >= 1 && n <= hi ? n : d;
}

static std::vector<Section> questScreen() {
    return {{"Virtual Screen", "", [](Rows& R) {
        static const char* sizes[] = {"", "Small", "Medium", "Large", "Huge"};
        static const char* dists[] = {"", "Near", "Normal", "Far"};
        int sz = setI("vrSize", 2, 4), di = setI("vrDistance", 2, 3);
        R.push("Screen Size", sizes[sz], [sz] { setS("vrSize", sz % 4 + 1); });
        R.push("Distance", dists[di], [di] { setS("vrDistance", di % 3 + 1); });
        R.note("To put the screen straight ahead of you again, hold the Meta button on the right controller.");
    }}};
}

static std::vector<Section> screen() {
    if (app().quest()) return questScreen();
    return {{"Display", "", [](Rows& R) {
                R.tog("Start AnyPS5.exe in Full Screen", [] { return jbool(app().H, "fullscreenStart"); }, [](bool v) {
                    app().H["fullscreenStart"] = v;
                    app().host.send(json{{"fullscreenStart", v}});
                });
                R.push("Full Screen Now", "F11", [] {
                    SDL_Window* w = app().window;
                    if (w) SDL_SetWindowFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
                });
            }},
        {"Streaming", "", [](Rows& R) { R.tog("Stream-Safe: Hide Folder Paths", [] { return sb("safe"); }, [](bool v) { setS("safe", v, true); }); }}};
}

static std::vector<Section> sound() {
    return {{"Menu Sounds", "", [](Rows& R) {
        R.tog("Menu Sounds", [] { return sb("sounds"); }, [](bool v) { setS("sounds", v); });
        json vj = app().settings.get("volume", 2);
        int v = vj.is_number() ? vj.get<int>() : 2;
        const char* names[] = {"", "Low", "Medium", "High"};
        R.push("Volume", v >= 1 && v <= 3 ? names[v] : "", [v] { setS("volume", v % 3 + 1); });
        R.tog("Trophy Sound", [] { return app().settings.get("trophySound", true) != false; }, [](bool v) { setS("trophySound", v); });
        R.note("The trophy sound plays with the pop-up over a game, and only while Menu Sounds is on.");
    }}};
}

static std::vector<std::string> padNames() {
    std::vector<std::string> out;
    const json& p = app().H.contains("pads") ? app().H["pads"] : json::array();
    if (p.is_array())
        for (auto& x : p)
            if (x.is_string()) out.push_back(x.get<std::string>());
    if (!out.empty()) return out;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++)
        if (const char* nm = SDL_GetGamepadNameForID(ids[i])) out.push_back(nm);
    SDL_free(ids);
    return out;
}

static std::vector<Section> pad() {
    return {{"Controllers", "", [](Rows& R) {
        R.sh("Connected");
        auto all = padNames();
        if (all.empty()) R.info("No Controller Found", "");
        for (auto& n : all) R.info(n, "");
        R.sh("Buttons");
        bool home = app().settings.get("psAction", "cc") == "home";
        R.push("PS / Home Button Opens", home ? "Home Screen" : "Control Center", [home] { setS("psAction", home ? "cc" : "home"); });
        if (app().quest())
            R.note(glyph::fill("Point a controller at the screen and pull its trigger to select. {move} moves, {ok} selects, {back} goes back, {options} opens "
                               "Options and {ps} opens the Control Center.", Source::Quest));
        else if (app().H.value("platform", std::string()) == "linux")
            R.note(glyph::fill("On this controller that is {ps}. On an Xbox controller it is the Xbox button, on a Nintendo controller the Home button; a controller "
                               "without one uses Start + Select together. In a game it leaves the game; hold it for 1 second to close the game straight away.", glyph::pad()));
        else
            R.note("In a game the PS button always opens the game menu. PlayStation controllers held by Steam Input or DS4Windows show as in use and their PS button cannot be read.");
    }}};
}

static std::vector<Section> data() {
    return {{"Game Library", "", [](Rows& R) {
                auto& all = app().library.all;
                R.info("Games Folder", rootText()).secret = true;
                R.info("Games Found", std::to_string(all.size()));
                int art = 0;
                for (auto& g : all) art += !g.icon.empty();
                R.info("With Box Art", std::to_string(art));
                R.tog("Show Games Without Box Art", [] { return sb("showNoArt"); }, [](bool v) {
                    setS("showNoArt", v, true);
                    app().router.host(json{{"libraryLoaded", true}});
                });
                R.push("Scan for New Games", "", [] { app().host.send(json{{"rescan", true}}); });
            }},
        {"Saved Data", "", [](Rows& R) {
             int n = 0, shown = 0;
             for (auto& g : app().library.all) n += g.saves > 0;
             R.info("Games With Saves", std::to_string(n));
             for (auto& g : app().library.all)
                 if (g.saves > 0 && shown++ < 12) R.info(g.title, std::to_string(g.saves) + " files");
             R.note("Saves live in each game's _sd folder and are shared by all users.");
         }}};
}

static std::vector<Section> pipe() {
    return {{"Pipeline", "", [](Rows& R) {
                R.tog("Pause the Test Pipeline", [] { return jbool(app().H, "paused"); }, [](bool v) { app().host.send(json{{"pause", v}}); });
                R.note("While you play a game started here the pipeline pauses by itself and resumes when the game closes.");
            }},
        {"Other Windows", "", [](Rows& R) {
             R.push("Open the Machines Tab", "", [] { app().host.send(json{{"machines", true}}); });
             R.push("Open the AnyPS5 Games Work App", "", [] { app().host.send(json{{"tab", "work"}}); });
         }}};
}

std::vector<Section> baseSections(const std::string& id) {
    if (id == "users") return usersSections();
    if (id == "access") return accessSections();
    if (id == "storage") return storage();
    if (id == "capture") return capture();
    if (id == "notes") return notes();
    if (id == "screen") return screen();
    if (id == "sound") return sound();
    if (id == "pad") return pad();
    if (id == "data") return data();
    if (id == "pipe") return pipe();
    if (id == "system") return systemSections();
    return {};
}

}
