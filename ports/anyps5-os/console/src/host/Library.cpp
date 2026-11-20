#include "host/Library.h"
#include <algorithm>
#include <cctype>

namespace host {

using json = nlohmann::json;

static std::string str(const json& j, const char* k) {
    auto it = j.find(k);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static int num(const json& j, const char* k) {
    auto it = j.find(k);
    return it != j.end() && it->is_number() ? it->get<int>() : 0;
}

Game Game::from(const json& j) {
    Game g;
    g.id = str(j, "id");
    g.title = str(j, "title");
    g.icon = str(j, "icon");
    g.bg = str(j, "bg");
    g.shot = str(j, "shot");
    g.status = str(j, "status");
    g.lastRun = str(j, "lastRun");
    g.folder = str(j, "folder");
    g.exe = str(j, "exe");
    g.fps = num(j, "fps");
    g.saves = num(j, "saves");
    g.raw = j;
    return g;
}

json parseJsAssignment(const std::string& text) {
    auto eq = text.find('=');
    if (eq == std::string::npos) return json();
    std::string s = text.substr(eq + 1);
    while (!s.empty() && (std::isspace((unsigned char)s.back()) || s.back() == ';')) s.pop_back();
    try {
        return json::parse(s);
    } catch (...) {
        return json();
    }
}

bool Library::parseJs(const std::string& text) {
    json arr = parseJsAssignment(text);
    if (!arr.is_array()) return false;
    fromJson(arr);
    return true;
}

void Library::fromJson(const json& arr) {
    all.clear();
    if (!arr.is_array()) return;
    for (auto& j : arr) if (j.is_object()) all.push_back(Game::from(j));
}

std::vector<Game> Library::visible(bool showNoArt) const {
    std::vector<Game> out;
    for (auto& g : all) if (showNoArt || !g.icon.empty()) out.push_back(g);
    return out;
}

std::vector<Game> Library::recent(bool showNoArt) const {
    auto v = visible(showNoArt);
    std::stable_sort(v.begin(), v.end(), [](const Game& a, const Game& b) { return a.lastRun > b.lastRun; });
    return v;
}

const Game* Library::byId(const std::string& id) const {
    for (auto& g : all) if (!id.empty() && g.id == id) return &g;
    return nullptr;
}

std::string exeStem(const std::string& path) {
    std::string s = path;
    auto sl = s.find_last_of("/\\");
    if (sl != std::string::npos) s = s.substr(sl + 1);
    std::string low;
    for (char c : s) low += (char)std::tolower((unsigned char)c);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, ".exe") == 0) low.resize(low.size() - 4);
    return low;
}

const Game* Library::byExeName(const std::string& name) const {
    std::string n = exeStem(name);
    for (auto& g : all) if (!g.exe.empty() && exeStem(g.exe) == n) return &g;
    return nullptr;
}

}
