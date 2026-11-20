#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace host {

struct Game {
    std::string id, title, icon, bg, shot, status, lastRun, folder, exe;
    int fps = 0;
    int saves = 0;
    nlohmann::json raw;
    static Game from(const nlohmann::json& j);
};

class Library {
public:
    std::vector<Game> all;
    bool parseJs(const std::string& text);
    void fromJson(const nlohmann::json& arr);
    std::vector<Game> visible(bool showNoArt) const;
    std::vector<Game> recent(bool showNoArt) const;
    const Game* byId(const std::string& id) const;
    const Game* byExeName(const std::string& name) const;
};

nlohmann::json parseJsAssignment(const std::string& text);
std::string exeStem(const std::string& path);

}
