#pragma once
#include "core/Anim.h"
#include "core/App.h"
#include "screens/collection.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct LibEntry {
    host::Game g;
    bool inLib = false;
    collection::State st = collection::State::Unknown;
    int pct = -1;
    bool playable() const;
};

std::vector<LibEntry> libraryEntries(int tab, const std::string& sort);
LibEntry libraryEntry(const host::Game& g);
LibEntry collectionEntry(const collection::Item& it);
void openCollection(const std::string& back);

struct MenuItem {
    std::string label;
    std::function<void()> run;
    bool sep = false;
    bool keep = false;
};

namespace psmenu {
void show(std::vector<MenuItem> items, Rect anchor, bool beside);
void replace(std::vector<MenuItem> items, Rect anchor, bool beside);
void swapItems(std::vector<MenuItem> items);
std::vector<MenuItem> forEntry(const LibEntry& e, bool fromLibrary);
std::vector<MenuItem> sortItems();
Rect homeTile();
}

namespace gameinfo {
void show(const LibEntry& e);
}

class Library : public Screen {
public:
    void enter() override;
    void update(double dt) override;
    bool action(const Action& a) override;
    void host(const json& m) override;
    std::vector<Hint> legend() const override;
    void draw(Draw& d) override;

    static Rect sortButton();
    void rebuild(bool keep);
    void sorted();

private:
    Rect tileRect(int i, float scroll) const;
    void moved(bool instant, bool quiet = false);
    void activate();
    void options();
    void drawHead(Draw& d);
    void drawTile(Draw& d, const LibEntry& e, Rect r, bool on);
    void drawBg(Draw& d);
    void scrollRows(float rows);
    std::string countText() const;
    const LibEntry* cur() const;

    std::vector<LibEntry> list_;
    int tab_ = 0;
    int sel_ = 0;
    Tween scroll_, nameOp_;
    std::string art_[2];
    Tween artOp_[2];
    int artOn_ = 0;
    float wheel_ = 0;
};

}
