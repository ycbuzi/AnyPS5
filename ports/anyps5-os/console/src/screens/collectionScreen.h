#pragma once
#include "core/Anim.h"
#include "core/App.h"
#include "screens/collection.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

class CollectionScreen : public Screen {
public:
    void enter() override;
    bool action(const Action& a) override;
    void host(const json& m) override;
    std::vector<Hint> legend() const override;
    void draw(Draw& d) override;

    void setBack(const std::string& id) { nextBack_ = id; }

private:
    struct Button {
        std::string label;
        std::function<void()> run;
    };

    std::vector<Button> buttons() const;
    const collection::Item* cur() const;
    void rebuild();
    void moved(bool instant);
    void activate(int i);
    void options();
    void scrollRows(float rows);
    void askServer() const;
    bool empty() const;
    Rect listRect() const;
    Rect rowRect(int i, float scroll) const;

    void drawBg(Draw& d);
    void drawHead(Draw& d);
    void drawBar(Draw& d, float y, bool centred);
    void drawRow(Draw& d, const collection::Item& it, Rect r, int i);
    void drawEmpty(Draw& d);

    std::vector<std::string> ids_;
    int sel_ = 0;
    int bar_ = 0;
    bool onBar_ = false;
    float wheel_ = 0;
    Tween scroll_;
    std::string back_ = "home", nextBack_;
};

}
