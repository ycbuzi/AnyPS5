#include "screens/search.h"
#include <algorithm>
#include <array>

namespace ui {

namespace {

const std::vector<std::array<const char*, 3>>& tabs() {
    static const std::vector<std::array<const char*, 3>> t = {{"games", "Games", "Search for games"},
                                                              {"media", "Media", "Search for captures and game art"},
                                                              {"players", "Players", "Search for players on this console"}};
    return t;
}

int tabIndex(const std::string& id) {
    for (size_t i = 0; i < tabs().size(); i++)
        if (id == tabs()[i][0]) return (int)i;
    return 0;
}

bool has(const std::string& hay, const std::string& q) { return osk::fold(hay).find(q) != std::string::npos; }

SearchResult result(const std::string& title, const std::string& icon, const std::string& kind, const json& item, bool hist = false) {
    SearchResult r;
    r.title = title;
    r.icon = icon;
    r.kind = kind;
    r.item = item;
    r.hist = hist;
    return r;
}

bool sameProfile(const json& a, const json& b) {
    if (!a.is_object() || !b.is_object()) return false;
    std::string ua = a.value("uid", std::string()), ub = b.value("uid", std::string());
    if (!ua.empty() && !ub.empty()) return ua == ub;
    return a.value("name", std::string()) == b.value("name", std::string());
}

}

const char* searchPlaceholder(const std::string& tab) { return tabs()[(size_t)tabIndex(tab)][2]; }
const char* searchTabLabel(int i) { return tabs()[(size_t)i][1]; }
const char* searchTabId(int i) { return tabs()[(size_t)i][0]; }
int searchTabCount() { return (int)tabs().size(); }

void Search::enter() {
    tab_ = "games";
    field_.set("");
    kb_.reset();
    oskSel_ = 0;
    focus_ = "osk";
    lsel_ = 0;
    scroll_ = 0;
    menu_.clear();
}

std::string Search::trimmed() const {
    std::string s = field_.str();
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> Search::history() const {
    std::vector<std::string> h;
    const json& me = app().me;
    if (me.is_object() && me.contains("searches") && me["searches"].is_array())
        for (auto& s : me["searches"])
            if (s.is_string()) h.push_back(s.get<std::string>());
    return h;
}

void Search::saveHistory(const std::vector<std::string>& h) {
    App& A = app();
    if (!A.me.is_object()) return;
    A.me["searches"] = h;
    for (auto& p : A.settings.profiles())
        if (sameProfile(p, A.me)) p["searches"] = h;
    A.saveSettings();
}

void Search::addHistory(const std::string& raw) {
    std::string q = raw;
    size_t a = q.find_first_not_of(" \t"), b = q.find_last_not_of(" \t");
    if (a == std::string::npos || !app().me.is_object()) return;
    q = q.substr(a, b - a + 1);
    std::vector<std::string> h{q};
    for (auto& x : history())
        if (osk::fold(x) != osk::fold(q)) h.push_back(x);
    if (h.size() > 10) h.resize(10);
    saveHistory(h);
}

void Search::dropHistory(int i) {
    auto h = history();
    if (i < 0) h.clear();
    else if (i < (int)h.size()) h.erase(h.begin() + i);
    saveHistory(h);
}

std::vector<SearchResult> Search::results() const {
    std::vector<SearchResult> out;
    App& A = app();
    std::string q = osk::fold(trimmed());
    if (q.empty()) {
        for (auto& h : history()) out.push_back(result(h, "", "", json(), true));
        if (!out.empty()) {
            SearchResult c = result("Clear Search History", "", "", json());
            c.clear = true;
            out.push_back(c);
        }
        return out;
    }
    if (tab_ == "media") {
        std::vector<SearchResult> all;
        if (A.media.is_object() && A.media.contains("items") && A.media["items"].is_array())
            for (auto& m : A.media["items"]) {
                std::string img = m.value("src", std::string()), title = m.value("game", std::string());
                json it{{"img", img}, {"title", title}, {"sub", m.value("sub", std::string())}, {"mid", m.value("id", std::string())}};
                all.push_back(result(title, img, "Media Gallery", it));
            }
        auto recent = A.library.all;
        std::stable_sort(recent.begin(), recent.end(), [](const host::Game& a, const host::Game& b) { return a.lastRun > b.lastRun; });
        for (auto& g : recent)
            if (!g.bg.empty()) all.push_back(result(g.title, g.bg, "Key Art", json{{"img", g.bg}, {"title", g.title}, {"sub", "Key art"}}));
        auto boxes = A.library.all;
        std::stable_sort(boxes.begin(), boxes.end(), [](const host::Game& a, const host::Game& b) { return a.title < b.title; });
        for (auto& g : boxes)
            if (!g.icon.empty()) all.push_back(result(g.title, g.icon, "Box Art", json{{"img", g.icon}, {"title", g.title}, {"sub", "Box art"}}));
        for (auto& r : all)
            if (has(r.title, q) || has(r.kind, q)) {
                r.title += " \xC2\xB7 " + r.kind;
                out.push_back(r);
            }
        return out;
    }
    if (tab_ == "players") {
        for (auto& p : A.settings.profiles()) {
            std::string n = p.value("name", std::string());
            if (!has(n, q)) continue;
            SearchResult r = result(sameProfile(p, A.me) ? n + " (you)" : n, "", "", json());
            r.player = p;
            r.round = true;
            out.push_back(r);
        }
        return out;
    }
    bool showNoArt = A.settings.get("showNoArt", false) == true;
    for (auto& g : A.library.visible(showNoArt))
        if (has(g.title, q) || has(g.id, q)) {
            SearchResult r = result(g.title, g.icon, "", json());
            r.game = json{{"id", g.id}, {"title", g.title}};
            out.push_back(r);
        }
    return out;
}

void Search::setTab(const std::string& t) {
    tab_ = t;
    lsel_ = 0;
    scroll_ = 0;
    if (focus_ == "grid" && results().empty()) focus_ = "osk";
}

void Search::key(const std::string& k) {
    if (k.empty()) return;
    if (k == "Done") {
        if (!trimmed().empty()) addHistory(field_.str());
        if (!results().empty()) focus_ = "grid";
    } else kb_.edit(field_, k, 60, false);
    lsel_ = 0;
    scroll_ = 0;
}

void Search::pick(const std::vector<SearchResult>& r, int i) {
    if (i < 0 || i >= (int)r.size()) return;
    const SearchResult& x = r[(size_t)i];
    App& A = app();
    if (x.clear) {
        dropHistory(-1);
        focus_ = "osk";
        lsel_ = 0;
        return;
    }
    if (x.hist) {
        field_.set(x.title);
        lsel_ = 0;
        scroll_ = 0;
        focus_ = results().empty() ? "osk" : "grid";
        addHistory(x.title);
        return;
    }
    addHistory(field_.str());
    if (!x.item.is_null()) {
        json items = json::array();
        for (auto& y : r) items.push_back(y.item);
        A.viewer = json{{"items", items}, {"index", i}};
        A.open("viewer");
        return;
    }
    if (!x.player.is_null()) {
        json p = x.player;
        auto switchUser = [] {
            app().me = nullptr;
            app().router.go("users");
        };
        if (sameProfile(p, A.me)) openMenu({{"Switch User", switchUser}, {"Cancel", [] {}, true}}, tileRect(i));
        else
            openMenu({{"Switch to " + p.value("name", std::string()), [p, switchUser] {
                           switchUser();
                           if (p.value("pinHash", std::string()).empty()) app().enterHome(p);
                           else {
                               app().login = p;
                               app().open("passcode");
                           }
                       }},
                      {"Cancel", [] {}, true}},
                     tileRect(i));
        return;
    }
    if (const host::Game* g = A.library.byId(x.game.value("id", std::string()))) A.play(*g);
}

void Search::openMenu(std::vector<SearchMenuItem> items, Rect anchor) {
    menu_ = std::move(items);
    msel_ = 0;
    menuAnchor_ = anchor;
}

std::vector<Hint> Search::legend() const {
    if (!menu_.empty()) return {{"Select", "ok"}, {"Back", "back"}};
    if (focus_ == "grid" && trimmed().empty()) return {{"Search", "ok"}, {"Delete", "square"}, {"Back", "back"}};
    return {{"Type", "ok"}, {"Delete", "square"}, {"Space", "space"}, {"Done", "enter"}, {"Back", "back"}};
}

bool Search::action(const Action& a) {
    const std::string& k = a.name;
    if (!menu_.empty()) return menuAction(k);
    if (k == "ps" || k == "help" || k == "create" || k == "fullscreen") return false;
    if (focus_ == "tabs") return tabsAction(k);
    if (focus_ == "osk") return oskAction(a);
    return gridAction(k);
}

bool Search::menuAction(const std::string& k) {
    if (k == "back") menu_.clear();
    else if (k == "up" && msel_ > 0) msel_--;
    else if (k == "down" && msel_ + 1 < (int)menu_.size()) msel_++;
    else if (k == "ok" || k == "enter") {
        auto run = menu_[(size_t)msel_].run;
        menu_.clear();
        if (run) run();
    }
    return true;
}

bool Search::tabsAction(const std::string& k) {
    int ti = tabIndex(tab_);
    if (k == "back") app().router.go("home");
    else if ((k == "left" || k == "l1") && ti > 0) setTab(tabs()[(size_t)ti - 1][0]);
    else if ((k == "right" || k == "r1" || k == "tab") && ti + 1 < (int)tabs().size()) setTab(tabs()[(size_t)ti + 1][0]);
    else if (k == "down" || k == "ok" || k == "enter") focus_ = "osk";
    return true;
}

bool Search::oskAction(const Action& a) {
    const std::string& k = a.name;
    if (k == "up" && kb_.move(oskSel_, "up") == oskSel_) {
        focus_ = "tabs";
        return true;
    }
    if (k == "back") app().router.go("home");
    else if (k == "square") key("\xE2\x8C\xAB");
    else if (k == "text") {
        std::u32string u = osk::decode(a.text);
        field_.text.insert((size_t)field_.cur, u);
        field_.cur += (int)u.size();
        lsel_ = 0;
        scroll_ = 0;
    } else if (k == "l1") key("Left");
    else if (k == "r1") key("Right");
    else if (k == "space" || k == "triangle") key("Space");
    else if (k == "enter" || k == "r2") key("Done");
    else if (k == "l2") key("Shift");
    else if (k == "ok") key(kb_.keyAt(oskSel_));
    else if (k == "down" && kb_.move(oskSel_, "down") == oskSel_) {
        if (!results().empty()) {
            focus_ = "grid";
            lsel_ = 0;
            scroll_ = 0;
        }
    } else if (k == "left" || k == "right" || k == "up" || k == "down") oskSel_ = kb_.move(oskSel_, k);
    return true;
}

bool Search::gridAction(const std::string& k) {
    auto list = results();
    int n = (int)list.size(), c = cols();
    if (k == "back") {
        focus_ = "osk";
        return true;
    }
    if (k == "square" && lsel_ < n && list[(size_t)lsel_].hist) {
        dropHistory(lsel_);
        int m = (int)results().size();
        if (!m) {
            focus_ = "osk";
            lsel_ = 0;
        } else lsel_ = std::min(lsel_, m - 1);
        return true;
    }
    if (k == "tab" || k == "r1" || k == "l1") {
        int ti = tabIndex(tab_), tn = (int)tabs().size();
        setTab(tabs()[(size_t)((ti + (k == "l1" ? tn - 1 : 1)) % tn)][0]);
        return true;
    }
    if (k == "left" && lsel_ % c) lsel_--;
    else if (k == "right" && lsel_ < n - 1 && lsel_ % c < c - 1) lsel_++;
    else if (k == "down" && lsel_ + c < n) lsel_ += c;
    else if (k == "up") {
        if (lsel_ >= c) lsel_ -= c;
        else focus_ = "osk";
    } else if (k == "ok" || k == "enter") pick(list, lsel_);
    return true;
}

void Search::hoverResult(int i) {
    focus_ = "grid";
    lsel_ = i;
}

void Search::clickResult(int i) {
    hoverResult(i);
    pick(results(), i);
}

ANYPS5_SCREEN(Search, "search");

}
