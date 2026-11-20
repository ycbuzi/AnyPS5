#include "screens/settings.h"
#include <algorithm>

namespace ui::sett {

Row& Rows::tog(const std::string& label, std::function<bool()> get, std::function<void(bool)> set) {
    Row r;
    r.label = label;
    r.toggle = get;
    r.run = [get, set] { set(!get()); };
    push_back(std::move(r));
    return back();
}

Row& Rows::info(const std::string& label, const std::string& val) {
    Row r;
    r.label = label;
    r.val = val;
    r.info = true;
    push_back(std::move(r));
    return back();
}

Row& Rows::sh(const std::string& t) {
    Row r;
    r.label = t;
    r.head = true;
    push_back(std::move(r));
    return back();
}

Row& Rows::note(const std::string& t) {
    Row r;
    r.label = t;
    r.note = true;
    push_back(std::move(r));
    return back();
}

Row& Rows::push(const std::string& label, const std::string& val, std::function<void()> run) {
    Row r;
    r.label = label;
    r.val = val;
    r.run = std::move(run);
    push_back(std::move(r));
    return back();
}

std::vector<OsArea>& osAreas() {
    static std::vector<OsArea> v;
    return v;
}

bool addOsArea(OsArea a) {
    auto& v = osAreas();
    auto it = std::find_if(v.begin(), v.end(), [&](const OsArea& x) { return x.id == a.id; });
    if (it != v.end()) *it = std::move(a);
    else v.push_back(std::move(a));
    std::stable_sort(v.begin(), v.end(), [](const OsArea& x, const OsArea& y) { return x.order < y.order; });
    return true;
}

bool areaOn(const std::string& id) {
    const json& h = app().H;
    if (!h.contains("areas") || !h["areas"].is_array()) return true;
    for (auto& x : h["areas"])
        if (x.is_string() && x.get<std::string>() == id) return true;
    return false;
}

static const std::vector<Cat>& baseCats() {
    static const std::vector<Cat> c = {{"access", "Accessibility"}, {"users", "Users and Accounts"}, {"system", "System"},
        {"storage", "Storage"}, {"sound", "Sound"}, {"screen", "Screen and Video"}, {"pad", "Accessories"},
        {"data", "Saved Data and Game Library"}, {"capture", "Captures and Broadcasts"}, {"notes", "Notifications"},
        {"pipe", "Test Pipeline"}};
    return c;
}

std::vector<Cat> allCats() {
    std::vector<Cat> out = baseCats();
    if (app().quest()) out.erase(std::remove_if(out.begin(), out.end(), [](const Cat& c) { return c.id == "pipe" || c.id == "capture"; }), out.end());
    for (auto& a : osAreas()) {
        if (!a.newCat || !areaOn(a.id)) continue;
        auto has = [&](const std::string& id) { return std::any_of(out.begin(), out.end(), [&](const Cat& c) { return c.id == id; }); };
        if (has(a.cat)) continue;
        auto at = std::find_if(out.begin(), out.end(), [&](const Cat& c) { return c.id == a.after; });
        size_t pos = at != out.end() ? (size_t)(at - out.begin()) + 1 : out.size() - 1;
        out.insert(out.begin() + pos, Cat{a.cat, a.catTitle.empty() ? a.title : a.catTitle});
    }
    return out;
}

std::string catIcon(const std::string& id) { return "set-" + id; }

std::vector<Section> catSections(const std::string& id) {
    std::vector<Section> out = baseSections(id);
    for (auto& a : osAreas()) {
        if (a.cat != id || !a.sections || !areaOn(a.id)) continue;
        for (auto& s : a.sections()) out.push_back(std::move(s));
    }
    return out;
}

void sys(const std::string& area, const std::string& action, const json& args, std::function<void(const json&)> done) {
    app().host.sys(area, action, args.is_object() ? args : json::object(), [done](const json& r) {
        if (done) done(r.is_object() ? r : json{{"ok", false}, {"error", "No answer"}});
    });
}

void saveApply() {
    app().saveSettings();
    app().applySettings();
}

void toast(const std::string& t) { app().toast(t); }

bool familyRestricted(const json& p) {
    if (!p.is_object() || !p.contains("family") || !p["family"].is_object()) return false;
    const json& f = p["family"];
    return jbool(f, "limited") || (f.contains("allowAll") && f["allowAll"] == false);
}

float textScale() {
    json v = app().settings.a11yv("text", "default");
    std::string s = v.is_string() ? v.get<std::string>() : "default";
    return s == "large" ? 1.15f : s == "larger" ? 1.3f : 1.f;
}

float tsz(float remSize) { return app().theme.r(remSize < 2.6f ? remSize * textScale() : remSize); }

json* profileByUid(const std::string& uid) { return uid.empty() ? nullptr : app().settings.profileByUid(uid); }

std::string jstr(const json& o, const char* k, const std::string& d) {
    if (!o.is_object() || !o.contains(k)) return d;
    const json& v = o[k];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return std::to_string(v.get<double>());
    return d;
}

bool jbool(const json& o, const char* k, bool d) {
    if (!o.is_object() || !o.contains(k) || !o[k].is_boolean()) return d;
    return o[k].get<bool>();
}

std::string failText(const json& r, const std::string& what) {
    if (jbool(r, "unsupported")) return AV;
    std::string e = jstr(r, "error");
    if (what.empty()) return e.empty() ? "Something went wrong" : e;
    return what + ": " + (e.empty() ? "no answer" : e);
}

static bool dnd_ = false;
bool dnd() { return dnd_; }
void setDnd(bool on) {
    dnd_ = on;
    app().host.send(json{{"dnd", on}});
}

}
