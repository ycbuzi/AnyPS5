#include "screens/TrophyData.h"
#include "core/App.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace ui::trophies {

const std::array<const char*, 4> kGrades = {"platinum", "gold", "silver", "bronze"};

int gradeIndex(const std::string& g) {
    for (size_t i = 0; i < kGrades.size(); i++)
        if (g == kGrades[i]) return (int)i;
    return -1;
}

int gradePoints(const std::string& g) {
    static const int pts[] = {300, 90, 30, 15};
    int i = gradeIndex(g);
    return i < 0 ? 15 : pts[i];
}

std::string gradeName(const std::string& g) {
    static const char* names[] = {"Platinum", "Gold", "Silver", "Bronze"};
    int i = gradeIndex(g);
    return i < 0 ? std::string() : names[i];
}

uint32_t gradeColor(const std::string& g) {
    static const uint32_t c[] = {0xb9d3f5, 0xe3b347, 0xc3c8d1, 0xc9875a};
    int i = gradeIndex(g);
    return i < 0 ? 0xffffff : c[i];
}

uint32_t levelColor(int lv) { return lv >= 999 ? 0xb9d3f5 : lv >= 600 ? 0xe3b347 : lv >= 300 ? 0xc3c8d1 : 0xc9875a; }

Level level(int points) {
    static const int tiers[][2] = {{99, 60}, {100, 90}, {100, 450}, {100, 900}, {100, 1350}, {100, 1800}, {100, 2250}, {100, 2700}, {100, 3150}, {100, 3600}};
    int lv = 1, left = points;
    for (auto& t : tiers) {
        int n = std::min(t[0], left / t[1]);
        lv += n;
        left -= n * t[1];
        if (n < t[0]) return {std::min(lv, 999), left, t[1]};
    }
    return {999, 0, 1};
}

Stats stats(const Set& s) {
    Stats st;
    st.total = (int)s.trophies.size();
    for (auto& t : s.trophies) {
        int p = gradePoints(t.grade), gi = gradeIndex(t.grade);
        st.max += p;
        if (gi >= 0) st.all[(size_t)gi]++;
        if (gi == 0) st.hasPlat = true;
        if (t.earned) {
            st.earned++;
            st.pts += p;
            if (gi >= 0) st.got[(size_t)gi]++;
            st.last = std::max(st.last, t.earned);
            if (gi == 0) st.plat = true;
        }
    }
    st.pct = st.max ? st.pts * 100 / st.max : 0;
    return st;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static int compareText(const std::string& a, const std::string& b) {
    int c = lower(a).compare(lower(b));
    if (!c) c = a.compare(b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

static std::string str(const json& o, const char* k) {
    auto it = o.find(k);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static int64_t num(const json& v) {
    if (v.is_number()) return (int64_t)v.get<double>();
    return 0;
}

void Store::reload() {
    app().host.get("/cache/trophies.js", [this](int status, const std::string& body) {
        if (status != 200) return;
        parse(body);
        app().router.host(json{{"trophiesLoaded", true}});
    });
}

void Store::parse(const std::string& body) {
    json d = host::parseJsAssignment(body);
    if (!d.is_object() || !d.contains("games") || !d["games"].is_array()) return;
    std::vector<Set> out;
    for (auto& g : d["games"]) {
        if (!g.is_object()) continue;
        Set s;
        s.key = str(g, "key");
        s.id = str(g, "id");
        s.title = str(g, "title");
        s.icon = str(g, "icon");
        s.label = g.contains("label") && g["label"].is_number() ? g["label"].get<int>() : -1;
        s.groups = g.contains("groups") && g["groups"].is_object() ? g["groups"] : json::object();
        if (g.contains("trophies") && g["trophies"].is_array())
            for (auto& t : g["trophies"]) {
                if (!t.is_object()) continue;
                Trophy x;
                x.id = t.contains("id") && t["id"].is_number() ? t["id"].get<int>() : 0;
                x.grade = str(t, "grade");
                x.hidden = t.value("hidden", false);
                x.group = t.contains("group") && t["group"].is_number() ? t["group"].get<int>() : -1;
                x.name = str(t, "name");
                x.detail = str(t, "detail");
                x.icon = str(t, "icon");
                x.e = t.contains("e") && t["e"].is_object() ? t["e"] : json::object();
                s.trophies.push_back(std::move(x));
            }
        out.push_back(std::move(s));
    }
    games_ = std::move(out);
    loaded_ = true;
    version_++;
    applyUser();
}

void Store::applyUser() {
    App& A = app();
    std::string uid = A.meUid();
    bool test = A.me.is_object() && A.settings.isTestUser(A.me);
    std::string who = uid + (test ? "#t" : "");
    if (who == user_ && applied_ == version_) return;
    user_ = who;
    applied_ = version_;
    for (auto& s : games_) {
        for (auto& t : s.trophies) {
            t.earned = 0;
            if (!A.me.is_object()) continue;
            if (!uid.empty() && t.e.contains(uid)) t.earned = num(t.e[uid]);
            if (!t.earned && test && t.e.contains("_tests")) t.earned = num(t.e["_tests"]);
        }
        s.st = stats(s);
    }
}

const Set* Store::byKey(const std::string& key) {
    applyUser();
    for (auto& s : games_)
        if (s.key == key) return &s;
    return nullptr;
}

const Set* Store::forGame(const std::string& gameId) {
    applyUser();
    for (auto& s : games_)
        if (s.id == gameId && s.label == 0) return &s;
    return nullptr;
}

static std::string lastRun(const std::string& id) {
    const host::Game* g = app().library.byId(id);
    return g ? g->lastRun : std::string();
}

std::vector<const Set*> Store::sets(const std::string& sort) {
    applyUser();
    std::vector<const Set*> l;
    for (auto& s : games_) {
        bool any = s.st.earned > 0;
        if (s.label != 0 && !any) continue;
        if (!any && lastRun(s.id).empty()) continue;
        l.push_back(&s);
    }
    if (sort == "az")
        std::stable_sort(l.begin(), l.end(), [](const Set* a, const Set* b) { return compareText(a->title, b->title) < 0; });
    else if (sort == "progress")
        std::stable_sort(l.begin(), l.end(), [](const Set* a, const Set* b) {
            if (a->st.pct != b->st.pct) return a->st.pct > b->st.pct;
            return compareText(a->title, b->title) < 0;
        });
    else
        std::stable_sort(l.begin(), l.end(), [](const Set* a, const Set* b) {
            if (a->st.last != b->st.last) return a->st.last > b->st.last;
            int c = lastRun(b->id).compare(lastRun(a->id));
            if (c) return c < 0;
            return compareText(a->title, b->title) < 0;
        });
    return l;
}

std::vector<const Trophy*> Store::list(const Set& s, const std::string& sort) const {
    std::vector<const Trophy*> l;
    for (auto& t : s.trophies) l.push_back(&t);
    if (sort == "earned")
        std::stable_sort(l.begin(), l.end(), [](const Trophy* a, const Trophy* b) {
            if (a->earned != b->earned) return a->earned > b->earned;
            return a->id < b->id;
        });
    else if (sort == "grade")
        std::stable_sort(l.begin(), l.end(), [](const Trophy* a, const Trophy* b) {
            int x = gradeIndex(a->grade), y = gradeIndex(b->grade);
            if (x != y) return x < y;
            return a->id < b->id;
        });
    else if (sort == "locked")
        std::stable_sort(l.begin(), l.end(), [](const Trophy* a, const Trophy* b) {
            bool x = a->earned != 0, y = b->earned != 0;
            if (x != y) return !x;
            return a->id < b->id;
        });
    return l;
}

Store& store() {
    static Store s;
    return s;
}

std::string when(int64_t ms) {
    std::time_t t = (std::time_t)(ms / 1000);
    std::tm* p = std::localtime(&t);
    if (!p) return std::string();
    std::tm tm = *p;
    char buf[64];
    if (app().settings.clock24())
        std::snprintf(buf, sizeof buf, "%d/%d/%d %02d:%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    else
        std::snprintf(buf, sizeof buf, "%d/%d/%d %d:%02d %s", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

int64_t parseLocal(const std::string& s) {
    std::tm tm{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    int n = std::sscanf(s.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
    if (n < 3) return 0;
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    return t < 0 ? 0 : (int64_t)t * 1000;
}

}
