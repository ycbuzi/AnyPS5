#include "screens/settings.h"
#include <algorithm>

namespace ui::sett {

namespace {

struct LangState {
    json st;
    bool loading = false, restart = false;
    std::string busy;
};

LangState L;

std::string localeLabel(const json& l) {
    std::string id = jstr(l, "id"), tag = jstr(l, "tag");
    std::string enc;
    size_t dot = id.find('.');
    if (dot != std::string::npos) {
        enc = id.substr(dot + 1);
        size_t at = enc.find('@');
        if (at != std::string::npos) enc = enc.substr(0, at);
    }
    std::string name = !tag.empty() ? langName(tag, id) : (id.rfind("C", 0) == 0 || id.rfind("POSIX", 0) == 0 ? "Plain (C locale)" : id);
    std::string e = enc;
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    if (!enc.empty() && e != "utf-8" && e != "utf8") name += " (" + enc + ")";
    return name;
}

const json& data() {
    static const json empty = json::object();
    return L.st.is_object() && L.st.contains("data") && L.st["data"].is_object() ? L.st["data"] : empty;
}

std::string currentLanguage() {
    const json& d = data();
    if (!d.contains("language") || !d["language"].is_object()) return "";
    const json& l = d["language"];
    if (!jstr(l, "name").empty()) return jstr(l, "name");
    if (!jstr(l, "locale").empty()) return localeLabel(json{{"id", jstr(l, "locale")}, {"tag", jstr(l, "tag")}});
    return "Not set";
}

void refresh() {
    if (L.loading) return;
    L.loading = true;
    sys("language", "status", {}, [](const json& r) {
        L.st = r;
        L.loading = false;
    });
}

void fail(const std::string& what, const json& r) {
    if (jbool(r, "unsupported")) toast(AV);
    else toast("Could not " + what + ": " + (jstr(r, "error").empty() ? "no answer" : jstr(r, "error")));
}

void pick(const std::string& listAction, const std::string& listKey, const std::string& setAction, const std::string& setKey,
    const std::string& title, std::function<std::string(const json&)> label, std::function<bool(const json&)> isCurrent,
    const std::string& doneText) {
    if (!L.busy.empty()) return;
    L.busy = "Loading " + title + "...";
    sys("language", listAction, {}, [=](const json& r) {
        L.busy.clear();
        if (!jbool(r, "ok")) {
            fail("load the list", r);
            return;
        }
        struct It { json it; std::string name; bool cur; };
        std::vector<It> items;
        const json& d = r.contains("data") ? r["data"] : json::object();
        if (d.contains(listKey) && d[listKey].is_array())
            for (auto& it : d[listKey]) items.push_back({it, label(it), isCurrent(it)});
        if (items.empty()) {
            toast("No choices were found");
            return;
        }
        std::sort(items.begin(), items.end(), [](const It& a, const It& b) { return a.cur != b.cur ? a.cur : a.name < b.name; });
        std::vector<MenuItem> m;
        for (auto& x : items) {
            json it = x.it;
            std::string name = x.name;
            bool cur = x.cur;
            m.push_back({name, [=] {
                             if (cur) return;
                             L.busy = "Changing " + title + "...";
                             sys("language", setAction, json{{setKey, jstr(it, "id")}}, [=](const json& r2) {
                                 L.busy.clear();
                                 if (!jbool(r2, "ok")) {
                                     fail("change it", r2);
                                     return;
                                 }
                                 bool rs = r2.contains("data") && jbool(r2["data"], "restart");
                                 if (rs) L.restart = true;
                                 toast(doneText + " " + name + (rs ? ". Restart AnyPS5 OS to apply." : "."));
                                 refresh();
                             });
                         },
                cur, jstr(it, "id")});
        }
        menu(m);
    });
}

void pickLanguage() {
    std::string cur;
    bool has = data().contains("language") && data()["language"].is_object();
    if (has) cur = jstr(data()["language"], "locale");
    pick("locales", "locales", "setLocale", "locale", "Console Language", localeLabel,
        [has, cur](const json& l) { return has && jstr(l, "id") == cur; }, "Console Language set to");
}

void pickLayout() {
    std::vector<std::string> cur;
    if (data().contains("keyboards") && data()["keyboards"].is_array())
        for (auto& k : data()["keyboards"]) cur.push_back(jstr(k, "id"));
    pick("layouts", "layouts", "setLayout", "layout", "Keyboard Layout",
        [](const json& l) { return jstr(l, "name").empty() ? jstr(l, "id") : jstr(l, "name"); },
        [cur](const json& l) { return cur.size() == 1 && cur[0] == jstr(l, "id"); }, "Keyboard Layout set to");
}

std::string kbList(const json& kb, const std::string& none) {
    std::string s;
    for (auto& k : kb) {
        if (!s.empty()) s += ", ";
        s += jstr(k, "name").empty() ? jstr(k, "id") : jstr(k, "name");
    }
    return s.empty() ? none : s;
}

void rows(Rows& R) {
    const char* note = "The AnyPS5 screen is in English; more languages come later.";
    if (!L.st.is_object() && !L.loading) refresh();
    if (!L.busy.empty()) R.info(L.busy, "");
    if (!L.st.is_object()) {
        R.info("Console Language", "Checking...");
        R.note(note);
        return;
    }
    if (!jbool(L.st, "ok")) {
        R.info("Console Language", jbool(L.st, "unsupported") ? AV : "Could not read: " + (jstr(L.st, "error").empty() ? "no answer" : jstr(L.st, "error")));
        R.note(note);
        return;
    }
    const json& d = data();
    bool win = jstr(d, "platform") == "windows";
    R.push("Console Language", currentLanguage(), pickLanguage);
    if (d.contains("region") && d["region"].is_object()) {
        const json& reg = d["region"];
        if (!jstr(reg, "name").empty() || !jstr(reg, "tag").empty())
            R.info("Region Format", !jstr(reg, "name").empty() ? jstr(reg, "name") : langName(jstr(reg, "tag"), jstr(reg, "locale")));
    }
    if (L.restart) R.note("Restart AnyPS5 OS to apply.");
    if (win) R.note("This is the Windows display language. Changing it from here is available in AnyPS5 OS.");
    R.note(note);
    R.sh("Input Languages / Keyboard Layout");
    json kb = d.contains("keyboards") && d["keyboards"].is_array() ? d["keyboards"] : json::array();
    if (win) {
        if (kb.empty()) R.info("Input Languages", "None found");
        for (auto& k : kb) {
            std::string ln = jstr(k, "langName").empty() ? jstr(k, "lang") : jstr(k, "langName");
            R.info(ln.empty() ? "Keyboard" : ln, jstr(k, "name").empty() ? jstr(k, "id") : jstr(k, "name"));
        }
        R.push("Keyboard Layout", kbList(kb, "None"), pickLayout);
    } else {
        R.push("Keyboard Layout", kbList(kb, "Not set"), pickLayout);
        if (!jstr(d, "x11Variant").empty()) R.info("Layout Variant", jstr(d, "x11Variant"));
        R.info("Text Console Keymap", jstr(d, "vcKeymap").empty() ? "Not set" : jstr(d, "vcKeymap"));
    }
}

OsArea make() {
    OsArea a;
    a.id = "language";
    a.cat = "system";
    a.title = "Language";
    a.order = 20;
    a.sections = [] { return std::vector<Section>{{"Language", "", rows}}; };
    a.refresh = refresh;
    return a;
}

}

ANYPS5_OS_AREA(language, make);

}
