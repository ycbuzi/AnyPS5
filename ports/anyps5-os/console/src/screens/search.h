#pragma once
#include "core/App.h"
#include "widgets/osk.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct SearchResult {
    std::string title, icon, kind;
    bool hist = false, clear = false, round = false;
    json item, player, game;
};

const char* searchPlaceholder(const std::string& tab);
const char* searchTabLabel(int i);
const char* searchTabId(int i);
int searchTabCount();

struct SearchMenuItem {
    std::string label;
    std::function<void()> run;
    bool sep = false;
};

class Search : public Screen {
public:
    void enter() override;
    bool action(const Action& a) override;
    bool typing() const override { return focus_ == "osk" && menu_.empty(); }
    std::vector<Hint> legend() const override;
    void draw(Draw& d) override;

private:
    std::vector<SearchResult> results() const;
    std::vector<std::string> history() const;
    void saveHistory(const std::vector<std::string>& h);
    void addHistory(const std::string& q);
    void dropHistory(int i);
    std::string trimmed() const;
    int cols() const { return trimmed().empty() ? 1 : 4; }
    void setTab(const std::string& t);
    void key(const std::string& k);
    void pick(const std::vector<SearchResult>& r, int i);
    bool tabsAction(const std::string& k);
    bool oskAction(const Action& a);
    bool gridAction(const std::string& k);
    bool menuAction(const std::string& k);
    void openMenu(std::vector<SearchMenuItem> items, Rect anchor);
    void drawTabs(Draw& d);
    void drawField(Draw& d);
    void drawGrid(Draw& d, const std::vector<SearchResult>& r);
    void drawHistory(Draw& d, const std::vector<SearchResult>& r);
    void drawMenu(Draw& d);
    void keepVisible(float top, float bottom, float viewH, float contentH);
    void keyTargets(float x, float y);
    Rect tileRect(int i) const;
    void hoverResult(int i);
    void clickResult(int i);

    std::string tab_ = "games", focus_ = "osk";
    osk::Field field_;
    osk::Keyboard kb_;
    int oskSel_ = 0, lsel_ = 0, msel_ = 0;
    float scroll_ = 0;
    std::vector<SearchMenuItem> menu_;
    Rect menuAnchor_{};
};

}
