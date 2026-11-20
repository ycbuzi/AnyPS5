#include "screens/settings.h"
#include <algorithm>

namespace ui::sett {

static std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool nameTaken(const std::string& n, const std::string& exceptUid) {
    for (auto& p : app().settings.profiles())
        if (p.value("uid", "") != exceptUid && lower(p.value("name", "")) == lower(n)) return true;
    return false;
}

static void openPin(const std::string& uid, const std::string& mode) {
    json* u = profileByUid(uid);
    if (!u) return;
    App& A = app();
    A.login = *u;
    A.login["pinMode"] = mode;
    if (!A.open("passcode")) toast("The passcode page is not available yet");
}

static void addUser() {
    if (familyRestricted(app().me)) {
        toast("Ask the family manager to add users (Settings > Users and Accounts)");
        return;
    }
    osk("Name for the new user", "", [](const std::string& raw) {
        std::string name = trim(raw);
        if (name.empty()) return;
        if (nameTaken(name, "\x01")) {
            toast("There is already a user called " + name);
            return;
        }
        json& P = app().settings.profiles();
        P.push_back(json{{"name", name}, {"color", (int)(P.size() % 6)}, {"pinHash", ""}});
        app().settings.ensureUids(app().host);
        saveApply();
        toast("Added " + name);
    });
}

static void userRows(const std::string& uid, Rows& R) {
    json* up = profileByUid(uid);
    if (!up) return;
    json u = *up;
    std::string name = u.value("name", "");
    bool restricted = familyRestricted(app().me);
    R.push("Name", name, [uid, name, restricted] {
        if (restricted) {
            toast("Only the family manager can rename users");
            return;
        }
        osk("New name for " + name, name, [uid](const std::string& raw) {
            std::string n = trim(raw);
            json* p = profileByUid(uid);
            if (n.empty() || !p) return;
            if (nameTaken(n, uid)) {
                toast("That name is taken");
                return;
            }
            if (!p->value("pinHash", "").empty()) {
                toast("Set the passcode again after a rename");
                (*p)["pinHash"] = "";
            }
            (*p)["name"] = n;
            saveApply();
        });
    });
    int color = u.value("color", 0);
    Row& c = R.push("Color", "", [uid] {
        json* p = profileByUid(uid);
        if (!p) return;
        (*p)["color"] = (p->value("color", 0) + 1) % 6;
        saveApply();
    });
    c.swatch = std::max(0, color);
    if (!u.value("pinHash", "").empty()) {
        R.push("Change Passcode", "", [uid] { openPin(uid, "change"); });
        R.push("Turn Off Passcode", "", [uid] { openPin(uid, "remove"); });
    } else {
        R.push("Set Passcode", "Off", [uid] { openPin(uid, "set"); });
    }
    if (app().settings.profiles().size() > 1 && uid != app().meUid() && !restricted)
        R.push("Delete User", "", [uid, name] {
            menu({{"Delete " + name, [uid, name] {
                      json& P = app().settings.profiles();
                      for (size_t i = 0; i < P.size(); i++)
                          if (P[i].value("uid", "") == uid) {
                              P.erase(P.begin() + i);
                              break;
                          }
                      saveApply();
                      openPage("users", "Users and Accounts");
                      toast("Deleted " + name);
                  }},
                {"Cancel", [] {}}});
        });
    R.note("The passcode locks this user on the AnyPS5 screen. It is stored only as a hash on this PC.");
}

std::vector<Section> usersSections() {
    std::vector<Section> out;
    for (auto& p : app().settings.profiles()) {
        std::string uid = p.value("uid", "");
        out.push_back({p.value("name", ""), uid, [uid](Rows& R) { userRows(uid, R); }});
    }
    out.push_back({"Login Settings", "", [](Rows& R) {
        App& A = app();
        if (!A.me.is_object()) return;
        std::string uid = A.meUid(), name = A.me.value("name", "");
        R.tog("Log In Automatically as " + name,
            [uid] { std::string a = app().settings.get("autoLogin", "").is_string() ? app().settings.get("autoLogin", "").get<std::string>() : ""; return !a.empty() && a == uid; },
            [uid, name](bool v) {
                json* p = profileByUid(uid);
                if (v && p && !p->value("pinHash", "").empty()) {
                    toast("Turn off " + name + "'s passcode first: logging in automatically skips it");
                    return;
                }
                app().settings.S["autoLogin"] = v ? uid : "";
                app().saveSettings();
            });
        R.note("When it is on, AnyPS5 skips the user screen at start and logs this user in. A user with a passcode cannot log in automatically.");
    }});
    out.push_back({"Add User", "", [](Rows& R) { R.push("Add a User", "+", addUser); }});
    return out;
}

}
