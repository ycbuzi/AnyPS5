#pragma once
#include "core/App.h"
#include "screens/TrophiesKit.h"
#include <cstdint>

namespace ui {

std::string gallerySort();
std::string gallerySortName(const std::string& key);

struct GalleryItem {
    std::string id, game, gameId, src, thumb, source, trophyGrade, trophyName;
    int64_t at = 0;
    bool album = false;
    int count = 0;
};

class Gallery : public Screen {
public:
    static void open(const std::string& tab = "all", const std::string& album = "", const std::string& from = "");
    static std::vector<std::string> favs();
    static void toggleFav(const std::string& id);
    static std::string mediaSub(const GalleryItem& m);

    void enter() override;
    bool action(const Action& a) override;
    void host(const json& m) override;
    std::vector<Hint> legend() const override;
    void draw(Draw& d) override;

private:
    struct Pending {
        bool set = false;
        std::string tab, album, from;
    };
    static Pending& pending();
    std::vector<GalleryItem> media() const;
    std::vector<GalleryItem> testShots() const;
    std::vector<GalleryItem> albums() const;
    std::vector<GalleryItem> items() const;
    int tabIndex() const;
    void setTab(int i);
    bool albumView() const { return tab_ == "albums" && album_.empty(); }
    void ok();
    void menu();
    void back();
    void drawTabs(Draw& d);
    void drawGrid(Draw& d, const std::vector<GalleryItem>& items);
    void drawTile(Draw& d, const GalleryItem& x, Rect r, bool sel, const std::vector<std::string>& fav);
    Rect tileRect(int i) const;

    std::string tab_ = "all", album_, focus_ = "grid", from_ = "home";
    int sel_ = 0;
    float scroll_ = 0;
    Rect sortBtn_{};
    kit::PsMenu menu_;
};

}
