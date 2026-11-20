#include "host/Settings.h"
#include "host/HostClient.h"
#include <cctype>
#include <random>

namespace host {

using json = nlohmann::json;

json Settings::defaults() {
    return json{{"profiles", json::array({json{{"name", "Blu"}, {"color", 0}, {"pinHash", ""}}, json{{"name", "Guest"}, {"color", 3}, {"pinHash", ""}}})},
                {"sounds", true}, {"volume", 2}, {"psAction", "cc"}, {"showNoArt", false}, {"safe", false}, {"libSort", "recent"}};
}

Settings::Settings() : S(defaults()) {}

bool Settings::load(const json& o) {
    if (!o.is_object() || !o.contains("profiles") || !o["profiles"].is_array() || o["profiles"].empty()) return false;
    json s = defaults();
    for (auto it = o.begin(); it != o.end(); ++it) s[it.key()] = it.value();
    S = s;
    return true;
}

void Settings::save(HostClient& h) { h.send(json{{"settings", S}}); }

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool Settings::isTestUser(const json& p) const {
    if (!p.is_object()) return false;
    std::string testUid = S.value("testUid", "");
    if (!testUid.empty()) return p.value("uid", "") == testUid;
    return lower(p.value("name", "")) == "anyps5";
}

void Settings::ensureUids(HostClient& h) {
    bool changed = false;
    static std::mt19937 rng{std::random_device{}()};
    const char* abc = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (auto& p : profiles()) {
        if (!p.value("uid", "").empty()) continue;
        std::string u = "u";
        for (int i = 0; i < 8; i++) u += abc[rng() % 36];
        p["uid"] = u;
        changed = true;
    }
    if (S.value("testUid", "").empty()) {
        for (auto& p : profiles())
            if (lower(p.value("name", "")) == "anyps5") {
                S["testUid"] = p["uid"];
                changed = true;
                break;
            }
    }
    if (changed) save(h);
    for (auto& p : profiles())
        if (!isTestUser(p)) {
            h.send(json{{"trophyOwner", p.value("uid", "")}});
            break;
        }
}

json* Settings::profileByUid(const std::string& uid) {
    for (auto& p : profiles()) if (!uid.empty() && p.value("uid", "") == uid) return &p;
    return nullptr;
}

json* Settings::profileByName(const std::string& name) {
    for (auto& p : profiles()) if (p.value("name", "") == name) return &p;
    return nullptr;
}

bool Settings::a11y(const std::string& k) const {
    auto it = S.find("a11y");
    if (it == S.end() || !it->is_object() || !it->contains(k)) return false;
    const json& v = (*it)[k];
    return v.is_boolean() ? v.get<bool>() : !v.is_null() && v != 0 && v != "";
}

json Settings::a11yv(const std::string& k, const json& d) const {
    auto it = S.find("a11y");
    if (it == S.end() || !it->is_object() || !it->contains(k)) return d;
    return (*it)[k];
}

void Settings::setA11y(const std::string& k, const json& v) {
    if (!S["a11y"].is_object()) S["a11y"] = json::object();
    S["a11y"][k] = v;
}

json Settings::noteOpt(const std::string& k, const json& d) const {
    auto it = S.find("notes");
    if (it == S.end() || !it->is_object() || !it->contains(k)) return d;
    return (*it)[k];
}

void Settings::setNoteOpt(const std::string& k, const json& v) {
    if (!S["notes"].is_object()) S["notes"] = json::object();
    S["notes"][k] = v;
}

json Settings::get(const std::string& k, const json& d) const {
    auto it = S.find(k);
    return it == S.end() || it->is_null() ? d : *it;
}

bool Settings::clock24() const { return get("clock24", false).is_boolean() && get("clock24", false).get<bool>(); }

}
