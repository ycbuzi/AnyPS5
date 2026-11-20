#pragma once
#include "core/App.h"
#include "screens/TrophiesKit.h"
#include "screens/TrophyData.h"

namespace ui {

std::string trophySetting(const char* key, const char* dflt);
std::string trophySortName(const std::string& key);

class Trophies : public Screen {
public:
    static void openGames(const std::string& from = "");
    static void openList(const std::string& key, const std::string& from = "");

    void enter() override;
    void update(double dt) override;
    bool action(const Action& a) override;
    void host(const json& m) override;
    std::vector<Hint> legend() const override;
    void draw(Draw& d) override;

private:
    struct Pending {
        bool set = false;
        std::string view, key, from, listFrom;
    };
    static Pending& pending();
    int count();
    void sortMenu();
    void back();
    void drawGames(Draw& d);
    void drawList(Draw& d);
    void drawGameRow(Draw& d, const trophies::Set& s, Rect r, bool sel);
    void drawCard(Draw& d, const trophies::Set& s, const trophies::Trophy& t, Rect r, bool sel);
    float sumItem(Draw& d, float right, float cy, const std::string& kind, const std::string& a, const std::string& b, int pct, bool measure);
    Rect listRect() const;

    std::string view_ = "games", key_, from_ = "home", listFrom_ = "games";
    int sel_ = 0, gamesSel_ = 0;
    float scroll_ = 0;
    double sinceLoad_ = 0;
    Rect headRect_{}, barRect_{};
    kit::PsMenu menu_;
};

}
