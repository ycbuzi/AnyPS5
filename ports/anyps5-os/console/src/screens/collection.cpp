#include "screens/collection.h"
#include "core/Anim.h"
#include "core/App.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace ui::collection {

namespace {

using json = nlohmann::json;

std::vector<Item> g_items;
bool g_loaded = false;
bool g_loading = false;
bool g_supported = true;
bool g_busy = false;
bool g_asked = false;
std::string g_server, g_error;
json g_device = json::object();
const void* g_owner = nullptr;
SDL_TimerID g_timer = 0;
std::map<std::string, double> g_pending;

bool held(const Item& n) {
    auto p = g_pending.find(n.id);
    if (p == g_pending.end()) return false;
    if (n.state == State::Installing || n.state == State::Installed || n.state == State::Failed || now() - p->second > 15) {
        g_pending.erase(p);
        return false;
    }
    return true;
}

std::string pick(const json& j, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (j.contains(k) && j[k].is_string() && !j[k].get<std::string>().empty()) return j[k].get<std::string>();
    return "";
}

int pickPct(const json& j) {
    for (const char* k : {"pct", "progress", "percent"}) {
        if (!j.contains(k) || !j[k].is_number()) continue;
        double v = j[k].get<double>();
        return (int)std::clamp(std::lround(v), 0L, 100L);
    }
    return -1;
}

std::string squash(std::string s) {
    std::string o;
    for (char c : s)
        if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    return o;
}

const json& obj(const json& j, const char* k) {
    static const json empty = json::object();
    return j.contains(k) && j[k].is_object() ? j[k] : empty;
}

Item* find(const std::string& id) {
    for (auto& it : g_items)
        if (it.id == id) return &it;
    return nullptr;
}

std::string titleOf(const std::string& id) {
    if (const Item* it = byId(id); it && !it->title.empty()) return it->title;
    if (const host::Game* g = app().library.byId(id)) return g->title;
    return id;
}

std::string iconOf(const std::string& id) {
    if (const host::Game* g = app().library.byId(id); g && !g->icon.empty()) return g->icon;
    if (const Item* it = byId(id)) return it->icon;
    return "";
}

Uint32 pollTick(void*, SDL_TimerID, Uint32 interval) {
    app().host.inject(json{{"collectionPoll", true}});
    return interval;
}

void publish() {
    app().host.inject(json{{"downloads", downloads()}, {"downloadsChanged", true}});
}

std::vector<State> states() {
    std::vector<State> s;
    for (auto& it : g_items) s.push_back(it.state);
    return s;
}

void settle(const std::vector<State>& before, bool fromList) {
    bool busy = std::any_of(g_items.begin(), g_items.end(), [](const Item& i) { return i.state == State::Installing; });
    bool finished = false;
    for (size_t i = 0; i < g_items.size() && i < before.size(); i++) {
        if (before[i] != State::Installing || g_items[i].state == State::Installing) continue;
        finished = true;
        const Item& it = g_items[i];
        std::string t = titleOf(it.id);
        if (it.state == State::Installed) app().toast(t + " is installed", "Downloads", iconOf(it.id));
        else if (it.state == State::Failed) app().toast(t + " did not install" + (it.status.empty() ? "" : ": " + it.status), "Downloads", iconOf(it.id));
        else app().toast(t + " stopped installing", "Downloads", iconOf(it.id));
    }
    if (busy && !g_timer) g_timer = SDL_AddTimer(1000, pollTick, nullptr);
    if (!busy && g_timer) {
        SDL_RemoveTimer(g_timer);
        g_timer = 0;
    }
    if (busy || g_busy) publish();
    if (busy != g_busy) app().host.send(json{{"downloads", true}});
    g_busy = busy;
    if (finished) app().reloadLibrary();
    if (finished && !fromList) refresh();
    app().router.host(json{{"collectionChanged", true}});
}

void stamp() {
    for (auto& it : g_items)
        if (it.state == State::Installing && it.started.empty()) it.started = app().clockText();
}

void merge(const std::vector<Item>& in, bool activeOnly) {
    auto before = states();
    for (auto& n : in) {
        Item* o = find(n.id);
        if (!o) {
            if (activeOnly && n.state != State::Installing) continue;
            g_items.push_back(n);
            before.push_back(n.state);
            continue;
        }
        if (activeOnly && o->state != State::Installing && n.state != State::Installing) continue;
        if (held(n)) continue;
        if (n.state != State::Unknown) o->state = n.state;
        o->pct = n.pct;
        o->phase = n.phase;
        if (!n.status.empty() || n.state != State::Installing) o->status = n.status;
        if (!n.title.empty()) o->title = n.title;
        if (!n.started.empty()) o->started = n.started;
    }
    stamp();
    settle(before, false);
}

void mergeFull(const std::vector<Item>& in) {
    std::vector<State> before;
    for (auto& n : in) {
        const Item* o = byId(n.id);
        before.push_back(o ? o->state : n.state);
    }
    std::vector<Item> next = in;
    for (auto& n : next) {
        const Item* old = byId(n.id);
        if (old && n.started.empty()) n.started = old->started;
        if (old && held(n)) {
            n.state = old->state;
            n.pct = old->pct;
            n.phase = old->phase;
            n.status = old->status;
        }
    }
    g_items = std::move(next);
    stamp();
    settle(before, true);
}

void fail(const json& r, const std::string& what) {
    if (r.value("unsupported", false)) app().toast(what + " is not available on this console yet");
    else app().toast(r.value("error", std::string()).empty() ? what + " did not work" : r.value("error", std::string()));
}

void setLocal(const std::string& id, State s, int pct, const std::string& status) {
    Item* it = find(id);
    if (!it) {
        Item n;
        n.id = id;
        if (const host::Game* g = app().library.byId(id)) {
            n.title = g->title;
            n.icon = g->icon;
            n.bg = g->bg;
        }
        g_items.push_back(n);
        it = &g_items.back();
    }
    auto before = states();
    it->state = s;
    it->pct = pct;
    it->phase.clear();
    it->status = status;
    it->started = s == State::Installing ? app().clockText() : "";
    settle(before, true);
}

void startJob(const std::string& id, const std::string& verb) {
    std::string t = titleOf(id);
    std::string what = verb == "reinstall" ? "Reinstalling " : verb == "update" ? "Updating " : "Installing ";
    app().toast(what + t, "Downloads", iconOf(id));
    g_pending[id] = now();
    setLocal(id, State::Installing, 0, "Waiting to build");
    app().host.sys("collection", verb, json{{"titleId", id}}, [id, what](const json& r) {
        if (!r.value("ok", false)) {
            g_pending.erase(id);
            fail(r, what.substr(0, what.size() - 1));
            refresh();
            return;
        }
        auto got = parseItems(r.value("data", json()));
        if (!got.empty()) merge(got, false);
    });
}

std::string pctText(int p) { return p >= 0 ? " " + std::to_string(p) + "%" : ""; }

}

State parseState(const json& j) {
    std::string s = squash(pick(j, {"state", "install", "installState"}));
    if (s.empty() && j.contains("status") && j["status"].is_string()) {
        std::string st = squash(j["status"].get<std::string>());
        if (st == "installed" || st == "notinstalled" || st == "installing" || st == "available") s = st;
    }
    if (s == "installed" || s == "ready" || s == "done") return State::Installed;
    if (s == "installing" || s == "downloading" || s == "queued" || s == "extracting" || s == "copying" || s == "building" || s == "waiting") return State::Installing;
    if (s == "failed" || s == "error") return State::Failed;
    if (s == "unsupported") return State::Unsupported;
    if (!s.empty()) return State::NotInstalled;
    if (j.value("installing", false)) return State::Installing;
    if (j.contains("installed") && j["installed"].is_boolean()) return j["installed"].get<bool>() ? State::Installed : State::NotInstalled;
    return State::Unknown;
}

std::vector<Item> parseItems(const json& data) {
    const json* arr = data.is_array() ? &data : nullptr;
    if (!arr && data.is_object())
        for (const char* k : {"games", "items", "collection", "jobs"})
            if (data.contains(k) && data[k].is_array()) {
                arr = &data[k];
                break;
            }
    std::vector<Item> out;
    if (!arr && data.is_object() && !pick(data, {"id", "titleId"}).empty()) {
        json one = json::array({data});
        return parseItems(one);
    }
    if (!arr) return out;
    for (const auto& j : *arr) {
        if (!j.is_object()) continue;
        Item it;
        it.id = pick(j, {"id", "titleId", "title_id"});
        if (it.id.empty()) continue;
        it.title = pick(j, {"title", "name"});
        std::string file = pick(j, {"iconFile"});
        it.icon = !file.empty() ? "file:" + file : pick(j, {"icon", "art"});
        it.bg = pick(j, {"bg", "shot"});
        it.started = pick(j, {"started", "since"});
        it.status = pick(j, {"message", "detail", "step"});
        it.phase = pick(j, {"phase"});
        it.version = pick(j, {"version"});
        if (j.contains("size") && j["size"].is_number()) it.size = j["size"].get<long long>();
        it.source = pick(j, {"source"});
        if (it.source.empty()) it.source = pick(obj(j, "source"), {"type"});
        const json& c = obj(j, "compat");
        it.compat = pick(c, {"status"});
        it.compatReason = pick(c, {"reason"});
        const json& a = obj(j, "action");
        it.actionId = pick(a, {"id"});
        it.actionLabel = pick(a, {"label"});
        it.actionReason = pick(a, {"reason"});
        it.actionEnabled = a.value("enabled", false);
        it.update = j.value("update", false) == true;
        it.updateReason = pick(j, {"updateReason"});
        it.installed = j.contains("installed") && j["installed"].is_boolean() && j["installed"].get<bool>();
        if (j.contains("build") && j["build"].is_object()) it.build = j["build"];
        if (j.contains("sources") && j["sources"].is_array())
            for (const auto& s : j["sources"])
                if (s.is_object()) it.sources.push_back({pick(s, {"type"}), pick(s, {"label"}), pick(s, {"version"})});
        it.state = parseState(j);
        it.pct = pickPct(j);
        if (it.state == State::Unknown && it.pct >= 0 && it.pct < 100) it.state = State::Installing;
        it.raw = j;
        out.push_back(it);
    }
    return out;
}

bool supported() { return g_supported; }
bool loaded() { return g_loaded; }
bool loading() { return g_loading; }
const std::vector<Item>& items() { return g_items; }
std::string server() { return g_server; }
std::string listError() { return g_error; }
const json& device() { return g_device; }

const Item* byId(const std::string& id) {
    for (auto& it : g_items)
        if (it.id == id) return &it;
    return nullptr;
}

std::string label(const Item& it) {
    switch (it.state) {
    case State::Installed: return it.update ? "Update available" : "Installed";
    case State::NotInstalled: return it.compat == "nosource" ? "No game data" : "Not installed";
    case State::Failed: return "Failed";
    case State::Unsupported: return it.compat == "needs-newer" ? "Needs a newer AnyPS5" : "Not supported";
    case State::Installing:
        if (it.phase == "copying") return "Copying" + pctText(it.pct);
        if (it.phase == "building") return "Building" + pctText(it.pct);
        if (it.pct >= 0) return "Installing" + pctText(it.pct);
        return it.status.empty() ? "Installing" : it.status;
    default: return "";
    }
}

std::string labelFor(const std::string& id) {
    const Item* it = byId(id);
    return it ? label(*it) : "";
}

std::string sourceName(const Source& s) {
    if (s.type == "device") return "this device";
    if (s.type == "media") return s.label.empty() ? "USB drive" : "USB drive (" + s.label + ")";
    if (s.type == "server") return "PC library";
    return s.label.empty() ? s.type : s.label;
}

std::string sourcesText(const Item& it) {
    std::string out;
    std::vector<std::string> seen;
    for (auto& s : it.sources) {
        std::string n = s.type == "media" ? "USB drive" : sourceName(s);
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        out += (out.empty() ? "" : ", ") + n;
    }
    if (out.empty() && !it.source.empty()) out = sourceName({it.source, "", ""});
    return out;
}

std::string sizeText(long long b) {
    if (b < 0) return "";
    char buf[32];
    if (b >= (1LL << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / double(1LL << 30));
    else if (b >= (1LL << 20)) std::snprintf(buf, sizeof buf, "%.0f MB", b / double(1LL << 20));
    else std::snprintf(buf, sizeof buf, "%lld KB", (b + 1023) / 1024);
    return buf;
}

std::string sentence(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

std::string shortCommit(const std::string& c) { return c.substr(0, std::min<size_t>(7, c.size())); }

std::string compatText(const Item& it) {
    std::string r = it.compatReason;
    if (it.compat == "installable") return "Works on this device. AnyPS5 builds it here for this device.";
    if (it.compat == "unsupported") return "Not supported on this device" + (r.empty() ? std::string(".") : ": " + r + ".");
    if (it.compat == "needs-newer") return "Needs a newer AnyPS5" + (r.empty() ? std::string(".") : ": " + r + ".");
    if (it.compat == "nosource") return "No game data found. Plug in the drive with this game, or connect to your PC.";
    return "";
}

std::string buildText(const Item& it) {
    if (!it.build.is_object()) return it.state == State::Installed ? "Installed" : "Not installed";
    std::string commit = pick(it.build, {"toolchain"}), date = pick(it.build, {"toolchainDate"}), gv = pick(it.build, {"gameVersion"});
    std::string s = commit.empty() ? (it.build.value("prebuilt", false) ? "Ready-made build" : "AnyPS5 build") : "AnyPS5 " + shortCommit(commit);
    if (!date.empty()) s += " (" + date.substr(0, std::min<size_t>(10, date.size())) + ")";
    if (!gv.empty()) s += ", game version " + gv;
    return s;
}

std::string deviceText() {
    const json& d = g_device;
    if (!d.is_object() || d.empty()) return "";
    const json& c = obj(d, "cpu");
    const json& t = obj(d, "toolchain");
    std::string s = pick(c, {"model"});
    std::string lvl = pick(c, {"level"});
    if (!lvl.empty()) s += (s.empty() ? "" : "  \xC2\xB7  ") + lvl;
    std::string tc = t.value("present", false) ? "AnyPS5 " + shortCommit(pick(t, {"commit"})) : "AnyPS5 tools not installed";
    if (t.value("present", false) && pick(t, {"commit"}).empty()) tc = "AnyPS5 tools";
    return s + (s.empty() ? "" : "  \xC2\xB7  ") + tc;
}

std::string verbOf(const Item& it) {
    if (it.state == State::Installing) return "cancel";
    std::string a = it.actionId == "cancel" ? "" : it.actionId;
    if (a.empty()) a = it.state == State::Installed ? "reinstall" : it.state == State::Installing ? "cancel" : it.state == State::Unsupported ? "none" : "install";
    return a;
}

std::string actionVerb(const Item& it) {
    std::string a = verbOf(it);
    if (a == "install") return "Install";
    if (a == "reinstall") return "Reinstall";
    if (a == "update") return "Update";
    if (a == "retry") return "Retry";
    if (a == "cancel") return "Cancel";
    return "";
}

void refresh() {
    g_asked = true;
    g_loading = true;
    app().host.sys("collection", "list", json::object(), [](const json& r) {
        g_loading = false;
        if (!r.value("ok", false)) {
            if (r.value("unsupported", false)) g_supported = false;
            else g_error = r.value("error", std::string());
            g_loaded = true;
            app().router.host(json{{"collectionChanged", true}});
            return;
        }
        g_supported = true;
        g_loaded = true;
        const json& d = r.value("data", json());
        g_server = d.is_object() ? pick(d, {"server"}) : "";
        g_error = d.is_object() ? pick(d, {"error"}) : "";
        g_device = d.is_object() ? obj(d, "device") : json::object();
        mergeFull(parseItems(d));
    });
}

void install(const std::string& id) { startJob(id, "install"); }

void reinstall(const std::string& id) { startJob(id, "reinstall"); }

void remove(const std::string& id) {
    std::string title = titleOf(id);
    app().host.sys("collection", "delete", json{{"titleId", id}}, [id, title](const json& r) {
        if (!r.value("ok", false)) {
            fail(r, "Deleting");
            return;
        }
        setLocal(id, State::NotInstalled, -1, "");
        app().toast(title + " was deleted", "Storage", iconOf(id));
        app().reloadLibrary();
        refresh();
    });
}

void cancel(const std::string& id) {
    app().host.sys("collection", "cancel", json{{"titleId", id}}, [id](const json& r) {
        if (!r.value("ok", false)) {
            fail(r, "Cancelling");
            return;
        }
        g_pending.erase(id);
        auto got = parseItems(r.value("data", json()));
        if (!got.empty()) merge(got, false);
        else refresh();
    });
}

void act(const std::string& id) {
    const Item* it = byId(id);
    if (!it) return;
    std::string a = verbOf(*it);
    if (a == "cancel") {
        cancel(id);
        return;
    }
    if (a == "none" || (!it->actionId.empty() && !it->actionEnabled)) {
        std::string why = !it->actionLabel.empty() ? it->actionLabel : label(*it);
        app().toast(titleOf(id) + ": " + why, "Downloads", iconOf(id));
        return;
    }
    startJob(id, a == "retry" ? "install" : a);
}

void setServer(const std::string& address) {
    app().host.sys("collection", "server", json{{"server", address}}, [](const json& r) {
        if (!r.value("ok", false)) {
            fail(r, "Setting the PC address");
            return;
        }
        const json& d = r.value("data", json());
        g_server = d.is_object() ? pick(d, {"server"}) : "";
        app().toast(g_server.empty() ? "PC library address cleared" : "Looking for games on " + g_server, "Your Collection");
        refresh();
    });
}

void onHost(const json& m, const void* who) {
    if (!m.is_object()) return;
    if (!g_owner) g_owner = who;
    if (who != g_owner) return;
    if (m.value("collectionPoll", false) && g_busy) {
        app().host.sys("collection", "status", json::object(), [](const json& r) {
            if (r.value("ok", false)) merge(parseItems(r.value("data", json())), true);
        });
    }
    if (m.contains("collection")) {
        const json& c = m["collection"];
        if (c.is_boolean()) {
            if (c.get<bool>()) refresh();
        } else {
            merge(parseItems(c), false);
        }
    }
    if (m.value("libraryLoaded", false) && !g_asked) refresh();
}

json downloads() {
    json arr = json::array();
    for (auto& it : g_items) {
        if (it.state != State::Installing) continue;
        json x{{"title", titleOf(it.id)}, {"kind", "install"}, {"gameId", it.id}, {"status", label(it)}};
        if (it.pct >= 0) x["pct"] = it.pct;
        if (!it.started.empty()) x["started"] = it.started;
        arr.push_back(x);
    }
    return json{{"source", "collection"}, {"items", arr}};
}

}
