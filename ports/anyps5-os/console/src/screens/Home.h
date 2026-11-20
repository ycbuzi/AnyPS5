#pragma once
#include "core/App.h"
#include "core/Anim.h"
#include "host/Library.h"
#include <string>
#include <vector>

namespace ui {

struct MediaCard {
    std::string img, thumb, title, sub, id;
};

struct HomeItem {
    bool lib = false;
    bool running = false;
    std::string media;
    std::string title, desc;
    host::Game game;
    std::vector<MediaCard> cards;
};

struct ArtLayer {
    std::string src;
    Tween op, zoom;
};

class Home : public Screen {
public:
    void enter() override;
    void host(const json& m) override;
    bool action(const Action& a) override;
    void draw(Draw& d) override;
    std::vector<Hint> legend() const override;

private:
    void rebuild();
    void setTab(const std::string& t);
    void moved();
    void layoutTargets(bool instant);
    void setArt(const std::string& src);
    void heroEnter();
    void activate();
    void openCard();
    void pickRow(int i);
    void pickAct(int i);
    void pickCard(int i);
    void stepRow(float rows);
    void stepCards(float rows);
    const HomeItem* cur() const;
    std::string artFor(const HomeItem& it) const;
    std::vector<HomeItem> gameItems() const;
    std::vector<HomeItem> mediaItems() const;

    void drawArt(Draw& d);
    void drawTop(Draw& d);
    void drawRow(Draw& d);
    void drawHero(Draw& d);
    void drawActs(Draw& d);
    void drawSide(Draw& d, const host::Game& g);
    void drawShelf(Draw& d, const HomeItem& it);
    void drawMediaShelf(Draw& d, const HomeItem& it);

    std::vector<HomeItem> items_;
    std::string tab_ = "games";
    int sel_ = 0;
    int selGames_ = 0, selMedia_ = 0;
    std::string focus_ = "row";
    int act_ = 0;
    std::vector<Tween> size_;
    Tween shift_, nameX_, cardShift_;
    ArtLayer art_[2];
    int artOn_ = 0;
    std::string heroKey_;
    Tween heroT_, metaT_;
    float rowWheel_ = 0, cardWheel_ = 0;
};

}
