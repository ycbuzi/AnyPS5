#include "screens/settings_a11yfx.h"
#include "widgets/Widgets.h"
#include "core/App.h"
#include "core/Images.h"
#include "core/Pointer.h"

namespace ui::widgets {

std::string initial(const nlohmann::json& p) {
    std::string n = p.is_object() ? p.value("name", std::string()) : std::string();
    if (n.empty()) return "?";
    unsigned char c = (unsigned char)n[0];
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    std::string s = n.substr(0, len);
    return text::upper(s);
}

void avatar(Draw& d, float cx, float cy, float dia, const nlohmann::json& p, float letterPx, Weight w) {
    const auto& cols = avatarColors();
    int ci = p.is_object() && p.contains("color") && p["color"].is_number() ? p["color"].get<int>() : 0;
    auto [a, b] = cols[(size_t)((ci % 6) + 6) % 6];
    Rect r{cx - dia / 2, cy - dia / 2, dia, dia};
    std::string pic = p.is_object() ? p.value("picture", std::string()) : std::string();
    SDL_Texture* t = pic.empty() ? nullptr : images::get(pic);
    if (t) {
        d.image(t, r, Fit::Cover, 1, dia / 2);
        return;
    }
    d.roundGradient(r, dia / 2, 135, a, b);
    text::draw(d, initial(p), cx, cy, {letterPx, w, white(), Align::Center, VAlign::Middle});
}

void clock(Draw& d, float right, float cy, float sizePx) {
    text::draw(d, app().clockText(), right, cy, {sizePx, Weight::Light, white(), Align::Right, VAlign::Middle});
}

void focusGlow(Draw& d, Rect r, float radius) {
    a11yfx::focus(r);
    pointer::target(r, nullptr, [] { app().act({"ok", ""}); }, radius);
    const Theme& t = app().theme;
    d.soft(r, radius, t.r(1.6f) * .5f, rgba(160, 200, 255, .55f));
    d.outline(r, radius, t.r(.19f), white());
}

void pill(Draw& d, Rect r, const std::string& label, float textPx, bool selected) {
    const Theme& t = app().theme;
    if (selected) {
        d.soft(r, r.h / 2, t.r(1.8f) * .5f, white(.35f));
        d.round(r, r.h / 2, white());
    } else {
        d.round(r, r.h / 2, rgba(36, 40, 52, .78f));
    }
    text::draw(d, label, r.x + r.w / 2, r.y + r.h / 2, {textPx, Weight::Regular, selected ? hex(0x0a0f1e) : white(), Align::Center, VAlign::Middle});
}

}
