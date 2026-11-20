#include "screens/settings.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <regex>

namespace ui::sett {

namespace {

const char* CAT = "family";
const std::vector<int> DUR = {30, 60, 90, 120, 180, 240, 300, 360, 0};

struct FamState {
    json usage;
    std::string usageState = "checking", selUid;
    bool busy = false;
};

FamState F;

struct Fam {
    bool limited = false, allowAll = true;
    int dailyMin = 0;
    std::string from, to, whenUp = "notify";
    std::vector<std::string> games;
};

bool hhmm(const std::string& s) { return s.size() == 5 && std::isdigit((unsigned char)s[0]) && std::isdigit((unsigned char)s[1]) && s[2] == ':' && std::isdigit((unsigned char)s[3]) && std::isdigit((unsigned char)s[4]); }

json& profiles() { return app().settings.profiles(); }
bool isTest(const json& u) { return u.is_object() && app().settings.isTestUser(u); }

Fam fam(const json& u) {
    Fam f;
    json o = u.is_object() && u.contains("family") && u["family"].is_object() ? u["family"] : json::object();
    f.limited = jbool(o, "limited");
    f.allowAll = o.value("allowAll", true) != false;
    f.dailyMin = o.contains("dailyMin") && o["dailyMin"].is_number() ? std::max(0, (int)std::lround(o["dailyMin"].get<double>())) : 0;
    f.from = hhmm(jstr(o, "from")) ? jstr(o, "from") : "";
    f.to = hhmm(jstr(o, "to")) ? jstr(o, "to") : "";
    f.whenUp = jstr(o, "whenUp") == "close" ? "close" : "notify";
    if (o.contains("games") && o["games"].is_array())
        for (auto& g : o["games"])
            if (g.is_string()) f.games.push_back(g.get<std::string>());
    return f;
}

void setFam(const std::string& uid, const json& patch) {
    json* u = profileByUid(uid);
    if (!u) return;
    Fam f = fam(*u);
    json o{{"limited", f.limited}, {"dailyMin", f.dailyMin}, {"from", f.from}, {"to", f.to}, {"allowAll", f.allowAll}, {"games", f.games}, {"whenUp", f.whenUp}};
    for (auto& [k, v] : patch.items()) o[k] = v;
    (*u)["family"] = o;
    app().saveSettings();
}

bool restricted(const json& u) {
    if (!u.is_object() || isTest(u)) return false;
    Fam f = fam(u);
    return f.limited || !f.allowAll;
}

std::string managerUid() {
    json mgr = app().settings.get("familyManager", "");
    if (mgr.is_string())
        if (json* p = profileByUid(mgr.get<std::string>()))
            if (!isTest(*p) && !restricted(*p)) return p->value("uid", "");
    for (auto& p : profiles())
        if (!p.value("uid", "").empty() && !isTest(p) && !restricted(p)) return p.value("uid", "");
    return "";
}

json meNow() {
    const json& me = app().me;
    if (!me.is_object()) return nullptr;
    if (json* p = profileByUid(me.value("uid", ""))) return *p;
    return me;
}

bool canManage() {
    json m = meNow();
    return m.is_object() && !m.value("uid", "").empty() && m.value("uid", "") == managerUid();
}

std::string pad2(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

std::tm localNow() {
    std::time_t t = app().wallNow();
    return *std::localtime(&t);
}

std::string todayStr() {
    std::tm tm = localNow();
    return std::to_string(tm.tm_year + 1900) + "-" + pad2(tm.tm_mon + 1) + "-" + pad2(tm.tm_mday);
}

int toMin(const std::string& t) { return hhmm(t) ? std::stoi(t.substr(0, 2)) * 60 + std::stoi(t.substr(3, 2)) : -1; }

std::string fmtMin(int m) {
    if (m < 60) return std::to_string(m) + " min";
    int h = m / 60, r = m % 60;
    return std::to_string(h) + " h" + (r ? " " + std::to_string(r) + " min" : "");
}

std::string durText(int m) {
    if (!m) return "No Limit";
    int h = m / 60, r = m % 60;
    std::string s;
    if (h) s += std::to_string(h) + (h == 1 ? " Hour" : " Hours");
    if (h && r) s += " ";
    if (r) s += std::to_string(r) + " Minutes";
    return s;
}

std::string windowText(const Fam& f) {
    if (f.from.empty() && f.to.empty()) return "Any Time";
    return (f.from.empty() ? "00:00" : f.from) + " to " + (f.to.empty() ? "24:00" : f.to);
}

int usedToday(const std::string& uid) {
    if (!F.usage.is_object() || jstr(F.usage, "date") != todayStr() || !F.usage.contains("today")) return -1;
    const json& t = F.usage["today"];
    return t.contains(uid) && t[uid].is_number() ? (int)t[uid].get<double>() : 0;
}

struct Win {
    bool inside;
    int toEnd;
};

Win windowState(const Fam& f) {
    std::tm tm = localNow();
    int n = tm.tm_hour * 60 + tm.tm_min, a = toMin(f.from), b = toMin(f.to);
    if (a < 0 && b < 0) return {true, -1};
    if (a >= 0 && b >= 0) {
        if (a == b) return {true, -1};
        if (a < b) return {n >= a && n < b, n >= a && n < b ? b - n : 0};
        if (n >= a) return {true, 1440 - n + b};
        if (n < b) return {true, b - n};
        return {false, 0};
    }
    if (a >= 0) return {n >= a, n >= a ? 1440 - n : 0};
    return {n < b, n < b ? b - n : 0};
}

int minutesLeft(const json& u) {
    if (!u.is_object() || isTest(u)) return -1;
    Fam f = fam(u);
    if (!f.limited) return -1;
    Win w = windowState(f);
    if (!w.inside) return 0;
    int left = INT_MAX;
    if (f.dailyMin > 0) left = std::max(0, f.dailyMin - std::max(0, usedToday(u.value("uid", ""))));
    if (w.toEnd >= 0) left = std::min(left, w.toEnd);
    return left == INT_MAX ? -1 : left;
}

std::string usageMissing() {
    return F.usageState == "unsupported" ? AV : F.usageState == "error" ? "Could not read playtime" : "Checking...";
}

std::string leftText(const json& u) {
    Fam f = fam(u);
    if (!f.limited) return "No Limit";
    if (f.dailyMin > 0 && usedToday(u.value("uid", "")) < 0) return usageMissing();
    int l = minutesLeft(u);
    return l < 0 ? "No Limit" : l == 0 ? "None" : fmtMin(l);
}

void refresh() {
    if (F.busy) return;
    F.busy = true;
    sys("family", "usage", {}, [](const json& r) {
        F.busy = false;
        if (jbool(r, "ok") && r.contains("data") && r["data"].is_object()) {
            F.usage = json{{"date", jstr(r["data"], "date")}, {"today", r["data"].value("today", json::object())}};
            F.usageState = "ok";
        } else F.usageState = jbool(r, "unsupported") ? "unsupported" : "error";
    });
}

json target() {
    json m = meNow();
    if (!canManage()) return m;
    json* u = profileByUid(F.selUid);
    if (!u || isTest(*u)) {
        std::string mu = managerUid();
        json pick;
        for (auto& p : profiles())
            if (!p.value("uid", "").empty() && !isTest(p) && p.value("uid", "") != mu && restricted(p)) {
                pick = p;
                break;
            }
        if (!pick.is_object())
            for (auto& p : profiles())
                if (!p.value("uid", "").empty() && !isTest(p) && p.value("uid", "") != mu) {
                    pick = p;
                    break;
                }
        if (!pick.is_object()) pick = m;
        F.selUid = pick.is_object() ? pick.value("uid", "") : "";
        return pick;
    }
    return *u;
}

bool editable(const json& u) { return u.is_object() && canManage() && u.value("uid", "") != managerUid() && !isTest(u); }

struct G {
    std::string id, title;
};

std::vector<G> games() {
    std::vector<G> out;
    for (auto& g : app().library.all) {
        if (g.id.empty() || std::any_of(out.begin(), out.end(), [&](const G& x) { return x.id == g.id; })) continue;
        out.push_back({g.id, g.title.empty() ? g.id : g.title});
    }
    std::sort(out.begin(), out.end(), [](const G& a, const G& b) { return a.title < b.title; });
    return out;
}

void askTime(const std::string& uid, const std::string& name, const std::string& key) {
    json* u = profileByUid(uid);
    if (!u) return;
    Fam f = fam(*u);
    std::string label = key == "from" ? "Playtime Start" : "Playtime End";
    std::string cur = key == "from" ? f.from : f.to;
    menu({{"Choose Time", [=] {
              osk(label + " (24-hour time, for example 19:30)", cur, [=](const std::string& t) {
                  std::smatch m;
                  static const std::regex re("^\\s*(\\d{1,2})\\s*[:.]?\\s*(\\d{2})\\s*$");
                  if (!std::regex_match(t, m, re) || std::stoi(m[1]) > 23 || std::stoi(m[2]) > 59) {
                      toast("Enter a time from 00:00 to 23:59");
                      return;
                  }
                  std::string v = pad2(std::stoi(m[1])) + ":" + m[2].str();
                  json* p = profileByUid(uid);
                  if (!p) return;
                  Fam g = fam(*p);
                  std::string other = key == "from" ? g.to : g.from;
                  if (!other.empty() && other == v) {
                      toast("Playtime Start and Playtime End must be different");
                      return;
                  }
                  setFam(uid, json{{key, v}});
                  toast(label + " for " + name + ": " + v);
              });
          }},
        {"Not Set", [=] {
             setFam(uid, json{{key, ""}});
             toast(label + " for " + name + ": not set");
         }}});
}

void membersRows(Rows& R) {
    json m = meNow();
    std::string mu = managerUid();
    json* mgr = profileByUid(mu);
    if (!m.is_object()) {
        R.note("Log in to see Family and Parental Controls.");
        return;
    }
    if (canManage()) {
        json t = target();
        R.sh("Family Members");
        for (auto& p : profiles()) {
            std::string uid = p.value("uid", ""), name = p.value("name", "");
            if (uid.empty() || isTest(p)) continue;
            std::string s = uid == mu ? "Family Manager" : restricted(p) ? "Restricted" : "No Restrictions";
            bool on = t.is_object() && t.value("uid", "") == uid;
            R.push(name, (on ? "\xE2\x9C\x93 " : "") + s, [uid, name] {
                F.selUid = uid;
                toast("Playtime Settings and Applications now show " + name);
            });
        }
        R.note("Pick a user, then set their Playtime Settings and Applications. The test pipeline user is never restricted.");
        R.sh("Family Manager");
        R.info("Family Manager", mgr ? mgr->value("name", "") : "");
        std::vector<std::pair<std::string, std::string>> others;
        for (auto& p : profiles())
            if (!p.value("uid", "").empty() && p.value("uid", "") != mu && !isTest(p) && !restricted(p)) others.push_back({p.value("uid", ""), p.value("name", "")});
        R.push("Change Family Manager", others.empty() ? "No Other User" : "", [others] {
            if (others.empty()) {
                toast("Only a user without restrictions can be the family manager");
                return;
            }
            std::vector<MenuItem> items;
            for (auto& o : others)
                items.push_back({o.second, [o] {
                                     app().settings.S["familyManager"] = o.first;
                                     app().saveSettings();
                                     toast(o.second + " is now the family manager");
                                 }});
            menu(items);
        });
        if (mgr && mgr->value("pinHash", "").empty())
            R.note("Set a passcode for " + mgr->value("name", "") + " in Users and Accounts > Users, so other users cannot log in as the family manager and change these settings.");
        return;
    }
    std::string mname = mgr ? mgr->value("name", "") : "";
    R.info("Family Manager", mgr ? mname : "None");
    R.note(mgr ? "Only " + mname + " can change Family and Parental Controls." : "No user without restrictions is set up. Add a new user in Users and Accounts to manage these settings.");
    R.sh("Your Restrictions");
    R.info("Status", restricted(m) ? "Restricted" : "No Restrictions");
    if (restricted(m)) {
        R.info("Playtime Left Today", leftText(m));
        Fam f = fam(m);
        R.info("Allowed Games", f.allowAll ? "All Games" : std::to_string(f.games.size()) + (f.games.size() == 1 ? " Game" : " Games"));
    }
}

void playtimeRows(Rows& R) {
    json u = target();
    if (!u.is_object()) {
        R.note("Log in to see playtime settings.");
        return;
    }
    Fam f = fam(u);
    std::string uid = u.value("uid", ""), name = u.value("name", "");
    R.sh(name);
    if (canManage() && uid == managerUid()) R.note(name + " is the family manager and has no restrictions. Pick another user in Family Members.");
    if (editable(u)) {
        R.tog("Restrict Playtime", [uid] { json* p = profileByUid(uid); return p && fam(*p).limited; }, [uid](bool v) { setFam(uid, json{{"limited", v}}); });
        int cur = f.dailyMin;
        R.push("Duration per Day", durText(f.dailyMin), [uid, cur] {
            std::vector<MenuItem> items;
            for (int d : DUR) items.push_back({durText(d), [uid, d] { setFam(uid, json{{"dailyMin", d}}); }, d == cur});
            menu(items);
        });
        R.push("Playtime Start", f.from.empty() ? "Not Set" : f.from, [uid, name] { askTime(uid, name, "from"); });
        R.push("Playtime End", f.to.empty() ? "Not Set" : f.to, [uid, name] { askTime(uid, name, "to"); });
        R.push("When Playtime Ends", f.whenUp == "close" ? "Close the Game" : "Notify Only", [uid] {
            menu({{"Notify Only", [uid] { setFam(uid, json{{"whenUp", "notify"}}); }}, {"Close the Game", [uid] { setFam(uid, json{{"whenUp", "close"}}); }}});
        });
    } else {
        R.info("Restrict Playtime", f.limited ? "On" : "Off");
        R.info("Duration per Day", durText(f.dailyMin));
        R.info("Playtime", windowText(f));
        R.info("When Playtime Ends", f.whenUp == "close" ? "Close the Game" : "Notify Only");
    }
    R.sh("Today");
    int used = usedToday(uid);
    R.info("Played Today", used >= 0 ? fmtMin(used) : usageMissing());
    R.info("Playtime Left Today", leftText(u));
    R.note("AnyPS5 counts each user's playtime while a game runs and resets it at midnight. Warnings appear 15, 5 and 1 minute before playtime ends. Playtime Start and End set the hours when games can be played.");
}

void appRows(Rows& R) {
    json u = target();
    if (!u.is_object()) {
        R.note("Log in to see application settings.");
        return;
    }
    std::string uid = u.value("uid", ""), name = u.value("name", "");
    auto list = games();
    Fam f = fam(u);
    R.sh(name);
    if (editable(u)) {
        R.tog("Allow All Games", [uid] { json* p = profileByUid(uid); return !p || fam(*p).allowAll; }, [uid](bool v) { setFam(uid, json{{"allowAll", v}}); });
        if (!f.allowAll) {
            R.info("Allowed Games", std::to_string(f.games.size()) + " of " + std::to_string(list.size()));
            R.sh("Games");
            if (list.empty()) R.note("No games in the library yet.");
            auto toggleGame = [uid](const std::string& id) {
                json* p = profileByUid(uid);
                if (!p) return;
                auto cur = fam(*p).games;
                auto it = std::find(cur.begin(), cur.end(), id);
                if (it != cur.end()) cur.erase(it);
                else cur.push_back(id);
                setFam(uid, json{{"games", cur}});
            };
            for (auto& g : list) {
                bool on = std::find(f.games.begin(), f.games.end(), g.id) != f.games.end();
                std::string id = g.id;
                R.push(g.title, on ? "\xE2\x9C\x93 Allowed" : "Not Allowed", [toggleGame, id] { toggleGame(id); });
            }
            for (auto& id : f.games)
                if (std::none_of(list.begin(), list.end(), [&](const G& g) { return g.id == id; }))
                    R.push(id, "\xE2\x9C\x93 Allowed (not in the library)", [toggleGame, id] { toggleGame(id); });
            R.note("Only games with a check mark can be started by " + name + ".");
        }
        return;
    }
    if (canManage() && uid == managerUid()) R.note(name + " is the family manager and can play every game.");
    R.info("Allow All Games", f.allowAll ? "On" : "Off");
    if (!f.allowAll) {
        R.sh("Allowed Games");
        if (f.games.empty()) R.note("No games are allowed.");
        for (auto& id : f.games) {
            auto it = std::find_if(list.begin(), list.end(), [&](const G& g) { return g.id == id; });
            R.info(it != list.end() ? it->title : id, "\xE2\x9C\x93");
        }
    }
}

OsArea make() {
    OsArea a;
    a.id = CAT;
    a.cat = CAT;
    a.title = "Family and Parental Controls";
    a.catTitle = "Family and Parental Controls";
    a.newCat = true;
    a.after = "users";
    a.order = 40;
    a.sections = [] { return std::vector<Section>{{"Family Management", "", membersRows}, {"Playtime Settings", "", playtimeRows}, {"Applications", "", appRows}}; };
    a.refresh = refresh;
    return a;
}

}

ANYPS5_OS_AREA(family, make);

}
