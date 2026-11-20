#include "screens/settings.h"
#include <cstdio>
#include <ctime>
#include <map>

namespace ui::sett {

namespace {

const char* SEC = "Date and Time";
const int PER = 9;
const std::vector<std::pair<std::string, std::string>> FORMATS = {{"system", "System Default"}, {"dmy", "DD/MM/YYYY"}, {"mdy", "MM/DD/YYYY"}, {"ymd", "YYYY-MM-DD"}};

struct DtState {
    json st;
    std::string err, busy;
    bool unsupported = false;
    json zones;
};

DtState D;

std::string dateFmt() {
    json f = app().settings.get("dateFmt", "system");
    return f.is_string() ? f.get<std::string>() : "system";
}

std::string fmtDate(std::time_t t) {
    std::tm tm = *std::localtime(&t);
    static const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    char b[64];
    std::string f = dateFmt();
    if (f == "dmy") std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    else if (f == "mdy") std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
    else if (f == "ymd") std::snprintf(b, sizeof b, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    else std::snprintf(b, sizeof b, "%s %d, %04d", months[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
    return b;
}

void refresh() {
    sys("datetime", "status", {}, [](const json& r) {
        if (jbool(r, "ok")) {
            D.st = r.contains("data") && r["data"].is_object() ? r["data"] : json::object();
            D.err.clear();
            D.unsupported = false;
        } else if (jbool(r, "unsupported")) {
            D.unsupported = true;
            D.err.clear();
            D.st = nullptr;
        } else {
            D.err = jstr(r, "error").empty() ? "No answer" : jstr(r, "error");
        }
    });
}

void finish(const json& r, const std::string& done) {
    D.busy.clear();
    if (jbool(r, "ok")) {
        if (r.contains("data") && r["data"].is_object() && r["data"].contains("platform")) D.st = r["data"];
        toast(done);
    } else toast(failText(r));
}

void setNtp(bool v) {
    if (!D.busy.empty()) return;
    D.busy = "ntp";
    sys("datetime", "setNtp", json{{"on", v}}, [v](const json& r) { finish(r, v ? "Date and time will be set automatically." : "Automatic date and time is off."); });
}

void setZone(const json& z) {
    if (!D.busy.empty()) return;
    D.busy = "tz";
    std::string label = jstr(z, "label");
    sys("datetime", "setTimezone", json{{"zone", jstr(z, "id")}}, [label](const json& r) { finish(r, "Time zone set to " + label + "."); });
}

struct PItem {
    std::string label, right;
    bool check = false;
    std::function<void()> fn;
};

void paged(std::vector<PItem> items, int page, std::function<void()> backFn) {
    int pages = std::max(1, (int)((items.size() + PER - 1) / PER)), start = page * PER;
    std::vector<MenuItem> m;
    for (int i = start; i < start + PER && i < (int)items.size(); i++) m.push_back({items[i].label, items[i].fn, items[i].check, items[i].right});
    if (page + 1 < pages) m.push_back({"Next Page", [=] { paged(items, page + 1, backFn); }, false, std::to_string(page + 2) + " of " + std::to_string(pages)});
    if (page > 0) m.push_back({"Previous Page", [=] { paged(items, page - 1, backFn); }, false, std::to_string(page) + " of " + std::to_string(pages)});
    if (backFn) m.push_back({"Back", backFn});
    menu(m, true);
}

void openRegions();

void openCities(const std::vector<json>& list, std::function<void()> backFn) {
    std::string cur = jstr(D.st, "timezone");
    std::vector<PItem> items;
    int at = 0;
    for (size_t i = 0; i < list.size(); i++) {
        json z = list[i];
        if (jstr(z, "id") == cur) at = (int)i;
        items.push_back({jstr(z, "label"), "", jstr(z, "id") == cur, [z] { setZone(z); }});
    }
    paged(items, at / PER, backFn);
}

void openRegions() {
    std::string cur = jstr(D.st, "timezone");
    std::vector<std::string> groups;
    std::map<std::string, std::vector<json>> by;
    for (auto& z : D.zones) {
        std::string g = jstr(z, "group");
        if (!by.count(g)) groups.push_back(g);
        by[g].push_back(z);
    }
    std::vector<PItem> items;
    for (auto& g : groups) {
        bool has = false;
        for (auto& z : by[g]) has |= jstr(z, "id") == cur;
        auto list = by[g];
        items.push_back({g, std::to_string(list.size()), has, [list] { openCities(list, openRegions); }});
    }
    paged(items, 0, nullptr);
}

void pickZone() {
    if (!D.busy.empty()) return;
    if (D.zones.is_array() && !D.zones.empty()) {
        openRegions();
        return;
    }
    D.busy = "zones";
    sys("datetime", "zones", {}, [](const json& r) {
        D.busy.clear();
        if (jbool(r, "ok") && r.contains("data") && r["data"].contains("zones") && r["data"]["zones"].is_array() && !r["data"]["zones"].empty()) {
            D.zones = r["data"]["zones"];
            if (onPage("system")) openRegions();
            return;
        }
        toast(jbool(r, "unsupported") ? AV : (jstr(r, "error").empty() ? "No time zones were found." : jstr(r, "error")));
    });
}

void pickFormat() {
    std::string cur = dateFmt();
    std::vector<MenuItem> m;
    for (auto& f : FORMATS) {
        std::string k = f.first;
        m.push_back({f.second, [k] {
                         app().settings.S["dateFmt"] = k;
                         app().saveSettings();
                     },
            k == cur});
    }
    menu(m, true);
}

std::string zoneText() {
    if (!D.st.is_object() || jstr(D.st, "timezone").empty()) return "Unknown";
    std::string l = jstr(D.st, "timezoneLabel");
    if (l.empty()) l = jstr(D.st, "timezone");
    std::string off = jstr(D.st, "utcOffset");
    return l + (off.empty() ? "" : " (UTC" + off + ")");
}

std::string yesNo(const json& o, const char* k) {
    if (!o.contains(k) || !o[k].is_boolean()) return "Unknown";
    return o[k].get<bool>() ? "Yes" : "No";
}

void rows(Rows& R) {
    const char* AUTO = "Set Date and Time Automatically";
    bool have = D.st.is_object();
    if (D.unsupported) R.info(AUTO, AV);
    else if (!D.err.empty()) {
        R.info(AUTO, "Could not check: " + D.err);
        R.push("Check Again", "", [] {
            D.err.clear();
            refresh();
        });
    } else if (!have) R.info(AUTO, "Checking...");
    else if (D.busy == "ntp") R.info(AUTO, "Setting...");
    else R.tog(AUTO, [] { return jbool(D.st, "ntp"); }, setNtp);
    if (have && !D.unsupported) {
        R.info("Time Synchronized", yesNo(D.st, "ntpSynchronized"));
        std::string src = jstr(D.st, "timeSource");
        if (src.empty()) src = jstr(D.st, "ntpServer");
        if (!src.empty()) R.info("Time Server", src);
        if (!jstr(D.st, "lastSync").empty()) R.info("Last Synchronized", jstr(D.st, "lastSync"));
    }
    std::time_t now = app().wallNow();
    R.info("Current Date and Time", fmtDate(now) + "  " + app().clockText());
    if (have && !jstr(D.st, "syncNote").empty()) R.note(jstr(D.st, "syncNote"));

    R.sh("Time Zone");
    if (D.unsupported) {
        R.info("Time Zone", "Unknown");
        R.info("Change Time Zone", AV);
    } else if (!have) R.info("Time Zone", D.err.empty() ? "Checking..." : "Unknown");
    else {
        R.push("Time Zone", D.busy == "zones" ? "Loading..." : D.busy == "tz" ? "Setting..." : zoneText(), pickZone);
        if (D.st.contains("autoTimezone") && D.st["autoTimezone"].is_boolean())
            R.info("Set Time Zone Automatically", std::string(jbool(D.st, "autoTimezone") ? "On" : "Off") + (jbool(D.st, "canSetAutoTimezone") ? "" : " (Available in AnyPS5 OS)"));
        if (D.st.contains("supportsDst") && D.st["supportsDst"] == false) R.info("Daylight Saving Time", "Not Used");
        else if (D.st.contains("dst") && D.st["dst"].is_boolean()) R.info("Daylight Saving Time", jbool(D.st, "dst") ? "In Effect" : "Not in Effect");
        if (D.st.value("canSetTimezone", true) == false || D.st.value("canSetNtp", true) == false)
            R.note(jstr(D.st, "platform") == "windows" ? "These follow Windows. Changing the time zone or automatic date and time is available in AnyPS5 OS." : "Changing automatic date and time needs a time sync service on this system.");
    }

    R.sh("Date and Time Format");
    R.tog("24-Hour Clock", [] { return app().settings.clock24(); }, [](bool v) {
        app().settings.S["clock24"] = v;
        app().saveSettings();
    });
    std::string fname = FORMATS[0].second;
    for (auto& f : FORMATS)
        if (f.first == dateFmt()) fname = f.second;
    R.push("Date Format", fname, pickFormat);
    R.note("Date Format changes the date shown on this page. Other screens show dates in the system format.");
}

OsArea make() {
    OsArea a;
    a.id = "datetime";
    a.cat = "system";
    a.title = SEC;
    a.order = 30;
    a.sections = [] { return std::vector<Section>{{SEC, "", rows}}; };
    a.refresh = refresh;
    return a;
}

}

ANYPS5_OS_AREA(datetime, make);

}
