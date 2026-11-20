#include "screens/Trophies.h"

namespace ui {

namespace {

const std::vector<std::pair<std::string, std::string>>& gameSorts() {
    static const std::vector<std::pair<std::string, std::string>> s = {{"recent", "Recently Earned"}, {"progress", "Progress"}, {"az", "Alphabetical"}};
    return s;
}

const std::vector<std::pair<std::string, std::string>>& trophySorts() {
    static const std::vector<std::pair<std::string, std::string>> s = {{"default", "Game Default"}, {"earned", "Date Earned"}, {"grade", "Grade"}, {"locked", "Not Earned First"}};
    return s;
}

}

std::string trophySetting(const char* k, const char* dflt) {
    const json& S = app().settings.S;
    auto it = S.find(k);
    return it != S.end() && it->is_string() && !it->get<std::string>().empty() ? it->get<std::string>() : std::string(dflt);
}

std::string trophySortName(const std::string& k) {
    for (auto& s : trophySorts())
        if (s.first == k) return s.second;
    return trophySorts()[0].second;
}

Trophies::Pending& Trophies::pending() {
    static Pending p;
    return p;
}

void Trophies::openGames(const std::string& from) {
    Pending& p = pending();
    p = {true, "games", "", from.empty() ? app().router.baseId() : from, "games"};
    app().open("trophies");
}

void Trophies::openList(const std::string& key, const std::string& from) {
    Pending& p = pending();
    p = {true, "list", key, from.empty() ? app().router.baseId() : from, from.empty() ? "games" : from};
    app().open("trophies");
}

void Trophies::enter() {
    Pending& p = pending();
    std::string from = p.set ? p.from : std::string("home");
    if (from != "trophies" && !from.empty()) from_ = from;
    if (from_.empty() || from_ == "boot" || from_ == "users") from_ = "home";
    if (p.set && p.view == "list") {
        if (view_ == "games") gamesSel_ = sel_;
        view_ = "list";
        key_ = p.key;
        listFrom_ = p.listFrom;
    } else {
        view_ = "games";
        listFrom_ = "games";
    }
    p = Pending{};
    sel_ = 0;
    scroll_ = 0;
    sinceLoad_ = 0;
    menu_.close();
    trophies::store().reload();
}

void Trophies::update(double dt) {
    sinceLoad_ += dt;
    if (sinceLoad_ > 120 && !app().ingame) {
        sinceLoad_ = 0;
        trophies::store().reload();
    }
}

void Trophies::host(const json& m) {
    if (m.value("trophiesChanged", false)) trophies::store().reload();
}

int Trophies::count() {
    auto& st = trophies::store();
    if (view_ == "games") return (int)st.sets(trophySetting("trSort", "recent")).size();
    const trophies::Set* s = st.byKey(key_);
    return s ? (int)s->trophies.size() : 0;
}

std::vector<Hint> Trophies::legend() const {
    if (menu_.open()) return kit::PsMenu::legend();
    return {{"Select", "ok"}, {"Sort", "options"}, {"Back", "back"}};
}

void Trophies::sortMenu() {
    bool games = view_ == "games";
    const char* key = games ? "trSort" : "trTSort";
    std::string cur = trophySetting(key, games ? "recent" : "default");
    std::vector<kit::MenuItem> items;
    for (auto& s : games ? gameSorts() : trophySorts()) {
        std::string k = s.first, label = (cur == k ? "\xE2\x9C\x93  " : "") + std::string("Sort by: ") + s.second;
        items.push_back({label, [this, key, k] {
                             app().settings.S[key] = k;
                             app().saveSettings();
                             sel_ = 0;
                         }});
    }
    items.push_back({"Cancel", nullptr, true});
    menu_.show(std::move(items), games ? headRect_ : barRect_);
}

void Trophies::back() {
    if (view_ == "list" && listFrom_ == "games") {
        view_ = "games";
        sel_ = gamesSel_;
        return;
    }
    std::string to = from_.empty() || from_ == "trophies" ? std::string("home") : from_;
    if (!app().open(to)) app().open("home");
}

bool Trophies::action(const Action& a) {
    if (menu_.open()) return menu_.action(a);
    int n = count();
    if (a.is("up")) {
        if (sel_ > 0) sel_--;
    } else if (a.is("down")) {
        if (sel_ < n - 1) sel_++;
    } else if (a.is("options")) {
        sortMenu();
    } else if (a.is("ok") || a.is("enter")) {
        if (view_ != "games") return true;
        auto sets = trophies::store().sets(trophySetting("trSort", "recent"));
        if (sel_ < (int)sets.size()) {
            gamesSel_ = sel_;
            listFrom_ = "games";
            view_ = "list";
            key_ = sets[(size_t)sel_]->key;
            sel_ = 0;
            scroll_ = 0;
        }
    } else if (a.is("back")) {
        back();
    } else {
        return a.is("left") || a.is("right");
    }
    return true;
}

ANYPS5_SCREEN(Trophies, "trophies");

}
