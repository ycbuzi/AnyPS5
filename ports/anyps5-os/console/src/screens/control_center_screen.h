#pragma once
#include "screens/control_center.h"

namespace ui::cc {

struct Card {
    std::string label, title, sub, img, icon, glyph, big, badge, hint;
    Run run;
};

enum class IconKind { Svg, Switcher, Avatar };

struct CcIcon {
    std::string label, svg;
    IconKind kind = IconKind::Svg;
    bool act = false;
    Run run;
};

class ControlCenter : public Screen {
public:
    bool overlay() const override { return true; }
    void enter() override;
    void update(double dt) override;
    bool action(const Action& a) override;
    void host(const json& m) override;
    void draw(Draw& d) override;

    std::vector<Card> cards();
    std::vector<CcIcon> icons();
    void closeCc(bool resume);
    void openMenu(int icon, const std::string& label, std::vector<MenuItem> items);
    void toHome();
    void hoverCard(int i);
    void hoverIcon(int i);

private:
    void drawCards(Draw& d, const std::vector<Card>& cs, float alpha, float dy);
    void drawCard(Draw& d, const Card& c, Rect r, bool sel);
    void drawHint(Draw& d, const Card& c, float cx);
    void drawIcons(Draw& d, const std::vector<CcIcon>& is, float dy);
    void layout(size_t n);

    int crow_ = 0, card_ = 0, qsel_ = 0;
    PanelHost panels_;
    Tween rise_, cardsA_, scroll_;
    std::vector<Tween> w_, h_;
    float scrollTarget_ = 0;
    double ping_ = 0;
    int lastSel_ = -1;
};

}
