#include "screens/Gallery.h"
#include "screens/Trophies.h"
#include <algorithm>
#include <cctype>
#include <map>

namespace ui {

namespace {

const std::vector<std::pair<std::string, std::string>>& tabs() {
    static const std::vector<std::pair<std::string, std::string>> t = {{"all", "All"}, {"fav", "Favorites"}, {"trophies", "Trophies"}, {"albums", "Albums"}};
    return t;
}

const std::vector<std::pair<std::string, std::string>>& sorts() {
    static const std::vector<std::pair<std::string, std::string>> s = {{"new", "Date (Newest)"}, {"old", "Date (Oldest)"}, {"game", "Game"}};
    return s;
}

std::string str(const json& o, const char* k) {
    auto it = o.find(k);
    if (it == o.end()) return std::string();
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    return std::string();
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool isTest(const std::string& id) { return id.rfind("test-", 0) == 0; }

}

std::string gallerySort() { return trophySetting("gSort", "new"); }

std::string gallerySortName(const std::string& k) {
    for (auto& s : sorts())
        if (s.first == k) return s.second;
    return sorts()[0].second;
}

Gallery::Pending& Gallery::pending() {
    static Pending p;
    return p;
}

void Gallery::open(const std::string& tab, const std::string& album, const std::string& from) {
    pending() = {true, tab, album, from.empty() ? app().router.baseId() : from};
    app().open("gallery");
}

std::vector<std::string> Gallery::favs() {
    std::vector<std::string> f;
    const json& me = app().me;
    if (!me.is_object() || !me.contains("favMedia") || !me["favMedia"].is_array()) return f;
    for (auto& v : me["favMedia"])
        if (v.is_string()) f.push_back(v.get<std::string>());
    return f;
}

void Gallery::toggleFav(const std::string& id) {
    App& A = app();
    if (!A.me.is_object()) return;
    auto f = favs();
    auto it = std::find(f.begin(), f.end(), id);
    if (it != f.end()) f.erase(it);
    else f.insert(f.begin(), id);
    json arr = f;
    json* p = A.settings.profileByUid(A.meUid());
    if (p) (*p)["favMedia"] = arr;
    A.me["favMedia"] = arr;
    A.saveSettings();
    if (p) A.applySettings();
}

std::string Gallery::mediaSub(const GalleryItem& m) {
    std::string s = m.source == "Trophy" ? "Trophy capture" : m.source == "Test run" ? "Test run" : m.source == "In game" ? "Screenshot (in the game)" : "Screenshot";
    return s + " \xC2\xB7 " + (m.at ? trophies::when(m.at) : std::string("date unknown"));
}

std::vector<GalleryItem> Gallery::media() const {
    std::vector<GalleryItem> out;
    const json& M = app().media;
    if (!M.is_object() || !M.contains("items") || !M["items"].is_array()) return out;
    for (auto& m : M["items"]) {
        if (!m.is_object()) continue;
        GalleryItem x;
        x.id = str(m, "id");
        x.game = str(m, "game");
        x.gameId = str(m, "gameId");
        x.src = str(m, "src");
        x.thumb = str(m, "thumb");
        x.source = str(m, "source");
        x.at = m.contains("at") && m["at"].is_number() ? (int64_t)m["at"].get<double>() : 0;
        if (m.contains("trophy") && m["trophy"].is_object()) {
            x.trophyGrade = str(m["trophy"], "grade");
            x.trophyName = str(m["trophy"], "name");
            if (x.trophyGrade.empty()) x.trophyGrade = "none";
        }
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<GalleryItem> Gallery::testShots() const {
    std::vector<GalleryItem> out;
    for (auto& g : app().library.all) {
        if (g.shot.empty()) continue;
        GalleryItem x;
        x.id = "test-" + g.id;
        x.game = g.title;
        x.gameId = g.id;
        x.src = x.thumb = g.shot;
        x.at = trophies::parseLocal(g.lastRun);
        x.source = "Test run";
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<GalleryItem> Gallery::albums() const {
    std::map<std::string, std::vector<GalleryItem>> by;
    std::vector<std::string> order;
    for (auto& m : media()) {
        if (!by.count(m.gameId)) order.push_back(m.gameId);
        by[m.gameId].push_back(m);
    }
    std::vector<GalleryItem> list;
    for (auto& id : order) {
        auto l = by[id];
        std::stable_sort(l.begin(), l.end(), [](const GalleryItem& a, const GalleryItem& b) { return a.at > b.at; });
        GalleryItem a;
        a.album = true;
        a.id = id;
        a.game = l[0].game;
        a.count = (int)l.size();
        a.thumb = l[0].thumb;
        a.at = l[0].at;
        list.push_back(std::move(a));
    }
    std::stable_sort(list.begin(), list.end(), [](const GalleryItem& a, const GalleryItem& b) { return a.at > b.at; });
    auto t = testShots();
    if (!t.empty()) {
        std::stable_sort(t.begin(), t.end(), [](const GalleryItem& a, const GalleryItem& b) { return a.at > b.at; });
        GalleryItem a;
        a.album = true;
        a.id = "_tests";
        a.game = "Test Runs";
        a.count = (int)t.size();
        a.thumb = t[0].thumb;
        list.push_back(std::move(a));
    }
    return list;
}

std::vector<GalleryItem> Gallery::items() const {
    if (albumView()) return albums();
    std::vector<GalleryItem> list;
    if (album_ == "_tests") list = testShots();
    else {
        auto f = tab_ == "fav" && album_.empty() ? favs() : std::vector<std::string>{};
        for (auto& m : media()) {
            if (!album_.empty()) {
                if (m.gameId != album_) continue;
            } else if (tab_ == "fav") {
                if (std::find(f.begin(), f.end(), m.id) == f.end()) continue;
            } else if (tab_ == "trophies") {
                if (m.trophyGrade.empty()) continue;
            }
            list.push_back(std::move(m));
        }
    }
    std::string k = gallerySort();
    if (k == "old") std::stable_sort(list.begin(), list.end(), [](const GalleryItem& a, const GalleryItem& b) { return a.at < b.at; });
    else if (k == "game")
        std::stable_sort(list.begin(), list.end(), [](const GalleryItem& a, const GalleryItem& b) {
            int c = lower(a.game).compare(lower(b.game));
            if (c) return c < 0;
            return a.at > b.at;
        });
    else std::stable_sort(list.begin(), list.end(), [](const GalleryItem& a, const GalleryItem& b) { return a.at > b.at; });
    return list;
}

int Gallery::tabIndex() const {
    for (size_t i = 0; i < tabs().size(); i++)
        if (tabs()[i].first == tab_) return (int)i;
    return 0;
}

void Gallery::setTab(int i) {
    tab_ = tabs()[(size_t)i].first;
    album_.clear();
    sel_ = 0;
    scroll_ = 0;
}

void Gallery::enter() {
    Pending& p = pending();
    std::string from = p.set ? p.from : std::string("home");
    if (from != "gallery") from_ = from.empty() || from == "boot" || from == "users" ? std::string("home") : from;
    tab_ = p.set && !p.tab.empty() ? p.tab : "all";
    album_ = p.set ? p.album : "";
    p = Pending{};
    sel_ = 0;
    scroll_ = 0;
    focus_ = "grid";
    menu_.close();
    app().reloadMedia();
}

void Gallery::host(const json& m) { (void)m; }

std::vector<Hint> Gallery::legend() const {
    if (menu_.open()) return kit::PsMenu::legend();
    return {{"Select", "ok"}, {"Options", "options"}, {"Back", "back"}};
}

void Gallery::ok() {
    auto list = items();
    if (sel_ >= (int)list.size()) return;
    const GalleryItem& x = list[(size_t)sel_];
    if (x.album) {
        album_ = x.id;
        sel_ = 0;
        scroll_ = 0;
        focus_ = "grid";
        return;
    }
    json v = json::array();
    for (auto& m : list) {
        std::string title = m.trophyGrade.empty() ? m.game : (m.trophyName.empty() ? std::string("Trophy") : m.trophyName) + " \xC2\xB7 " + m.game;
        v.push_back(json{{"img", m.src}, {"title", title}, {"sub", mediaSub(m)}, {"mid", isTest(m.id) ? std::string() : m.id}});
    }
    App& A = app();
    A.viewer = json{{"items", v}, {"index", sel_}};
    if (!A.open("viewer")) A.toast("The viewer is not available yet");
}

void Gallery::menu() {
    auto list = items();
    const GalleryItem* x = focus_ == "grid" && sel_ < (int)list.size() ? &list[(size_t)sel_] : nullptr;
    std::vector<kit::MenuItem> acts;
    if (x && !x->id.empty() && !x->album && !isTest(x->id)) {
        auto f = favs();
        bool on = std::find(f.begin(), f.end(), x->id) != f.end();
        std::string id = x->id;
        acts.push_back({on ? "Remove from Favorites" : "Add to Favorites", [id] { Gallery::toggleFav(id); }});
    }
    if (!albumView()) {
        std::string cur = gallerySort();
        bool first = true;
        for (auto& s : sorts()) {
            std::string k = s.first;
            acts.push_back({(cur == k ? "\xE2\x9C\x93  " : "") + std::string("Sort by: ") + s.second, [this, k] {
                                app().settings.S["gSort"] = k;
                                app().saveSettings();
                                sel_ = 0;
                            },
                            !acts.empty() && first});
            first = false;
        }
    }
    acts.push_back({"Cancel", nullptr, true});
    menu_.show(std::move(acts), x ? tileRect(sel_) : sortBtn_);
}

void Gallery::back() {
    if (!album_.empty()) {
        std::string a = album_;
        album_.clear();
        tab_ = "albums";
        focus_ = "grid";
        auto al = albums();
        sel_ = 0;
        for (size_t i = 0; i < al.size(); i++)
            if (al[i].id == a) sel_ = (int)i;
        return;
    }
    std::string to = from_.empty() || from_ == "gallery" ? std::string("home") : from_;
    if (!app().open(to)) app().open("home");
}

bool Gallery::action(const Action& a) {
    if (menu_.open()) return menu_.action(a);
    auto list = items();
    int n = (int)list.size(), cols = app().theme.W() * 3 <= app().theme.H() * 4 ? 2 : 3, ti = tabIndex(), nt = (int)tabs().size();
    if (!n && focus_ == "grid") focus_ = album_.empty() ? "tabs" : "side";
    const std::string& k = a.name;
    if (k == "options") {
        menu();
        return true;
    }
    if (k == "l1" || k == "r1" || k == "tab") {
        if (album_.empty()) setTab((ti + (k == "l1" ? nt - 1 : 1)) % nt);
        return true;
    }
    if (k == "back") {
        back();
        return true;
    }
    bool nav = k == "left" || k == "right" || k == "up" || k == "down" || k == "ok" || k == "enter";
    if (!nav) return false;
    if (focus_ == "tabs") {
        if (k == "left" && ti > 0) setTab(ti - 1);
        else if (k == "right" && ti < nt - 1) setTab(ti + 1);
        else if ((k == "down" || k == "ok" || k == "enter") && n) focus_ = "grid";
        else if (k == "down") focus_ = "side";
        return true;
    }
    if (focus_ == "side") {
        if (k == "right" && n) focus_ = "grid";
        else if (k == "up" && album_.empty()) focus_ = "tabs";
        else if (k == "ok" || k == "enter") menu();
        return true;
    }
    if (k == "left") {
        if (sel_ % cols) sel_--;
        else focus_ = "side";
    } else if (k == "right") {
        if (sel_ % cols < cols - 1 && sel_ < n - 1) sel_++;
    } else if (k == "down") {
        if (sel_ + cols < n) sel_ += cols;
    } else if (k == "up") {
        if (sel_ >= cols) sel_ -= cols;
        else if (album_.empty()) focus_ = "tabs";
    } else {
        ok();
    }
    return true;
}

ANYPS5_SCREEN(Gallery, "gallery");

}
