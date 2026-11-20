#pragma once
#include "core/App.h"
#include "core/Images.h"
#include "core/Text.h"
#include "widgets/Widgets.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui::cc {

using Run = std::function<void()>;

enum class Place { Cc, Above, Center };

class Panel {
public:
    virtual ~Panel() = default;
    virtual float width() const = 0;
    virtual float height() const = 0;
    virtual int count() const = 0;
    virtual void draw(Draw& d, Rect r) = 0;
    virtual Run pick() = 0;
    virtual bool key(const Action& a) { return false; }
    virtual void moved() {}
    virtual Color background() const { return rgba(36, 38, 46, .97f); }
    virtual float radius() const { return app().theme.r(.3f); }
    void hoverRow(int i) {
        if (i == sel || i < 0 || i >= count()) return;
        sel = i;
        moved();
    }
    int sel = 0;
    std::string head;
    Place place = Place::Cc;
    float anchorX = -1;
    Rect anchor{0, 0, 0, 0};
};

class PanelHost {
public:
    void open(std::unique_ptr<Panel> p);
    void close();
    bool on() const { return (bool)panel_; }
    Panel* get() const { return panel_.get(); }
    bool action(const Action& a);
    void update();
    void draw(Draw& d);
    Rect box() const;
    float dim() const { return dim_.value(); }

private:
    std::unique_ptr<Panel> panel_;
    std::vector<std::unique_ptr<Panel>> old_;
    Tween in_, dim_;
    float wheel_ = 0;
};

struct MenuItem {
    std::string label;
    Run run;
    bool sep = false;
};

std::unique_ptr<Panel> menu(const std::string& head, std::vector<MenuItem> items, Place place, float anchorX = -1, Rect anchor = {0, 0, 0, 0});
std::unique_ptr<Panel> psMenu(std::vector<MenuItem> items, Rect anchor);
std::unique_ptr<Panel> notes(PanelHost& host, float anchorX, int sel);
std::unique_ptr<Panel> power(float anchorX);
std::unique_ptr<Panel> downloads(float anchorX, int sel);

struct Playing {
    std::string title, id, icon, bg;
    int fps = 0;
};
bool playing(Playing& out);

constexpr int kIcons = 10;
float iconX(int i);
float iconY();

void noteIngame(const json& m);
void drawIngame(Draw& d);
bool dndOn();
void setDnd(bool on);
void closeAll();
void enterRest();
void powerOff(bool restart);
std::string age(long long wall);
void drawPanelFrame(Draw& d, Rect r, float radius, Color bg);
void drawToggle(Draw& d, float x, float cy, bool on);
void drawRowSel(Draw& d, Rect r);
void drawThumb(Draw& d, Rect r, float radius, const std::string& url, Color bg);

struct DlItem {
    std::string title, kind, status, started, gameId;
    int pct = -1;
};
const std::vector<DlItem>& dlItems();
bool dlPaused();
void dlLoad(std::function<void()> done = {});
std::string dlKind(const std::string& kind);
std::string dlGlyph(const std::string& kind);
const host::Game* dlGame(const DlItem& x);

}
