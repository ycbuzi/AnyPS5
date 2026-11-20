#include "core/Toast.h"
#include "core/App.h"
#include "core/Images.h"
#include "core/Text.h"

namespace ui {

void Toasts::push(const std::string& text, const std::string& src, const std::string& icon) {
    Note n{text, src.empty() ? "AnyPS5" : src, icon, now(), (long long)app().wallNow()};
    history_.insert(history_.begin(), n);
    if (history_.size() > 30) history_.resize(30);
    if (!popups) return;
    Live l{n, {}};
    l.in.restart(0, 1, .25f, easeCss);
    live_.push_back(l);
    while (live_.size() > 3) live_.erase(live_.begin());
}

void Toasts::update() {
    for (size_t i = live_.size(); i-- > 0;)
        if (now() - live_[i].n.at > seconds + .2) live_.erase(live_.begin() + (long)i);
}

void Toasts::draw(Draw& d) {
    const Theme& t = app().theme;
    float right = t.W() - t.r(2), y = t.r(7.5f);
    TextStyle st{t.r(1.35f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    float icon = t.r(2.6f), padX = t.r(1.5f), padY = t.r(1), gap = t.r(1);
    float maxText = t.r(40) - 2 * padX - icon - gap;
    for (auto& l : live_) {
        std::string s = text::ellipsize(l.n.text, st.font(), maxText);
        float tw = text::width(s, st.font());
        float w = padX * 2 + icon + gap + tw, h = padY * 2 + icon;
        float k = l.in.value();
        Rect r{right - w + t.r(2) * (1 - k), y, w, h};
        d.pushAlpha(k);
        d.shadow(r, t.r(.8f), t.r(2), black(.5f), 0, t.r(.6f));
        d.round(r, t.r(.8f), rgba(20, 26, 46, .96f));
        Rect ib{r.x + padX, r.y + padY, icon, icon};
        SDL_Texture* img = l.n.icon.empty() ? nullptr : images::get(l.n.icon);
        if (img) {
            d.image(img, ib, Fit::Cover, 1, t.r(.4f));
        } else {
            d.round(ib, t.r(.4f), hex(0x14182a));
            d.icon(images::asset("logo-mark.png"), ib.x + icon / 2, ib.y + icon / 2, icon * .62f);
        }
        text::draw(d, s, ib.x + icon + gap, r.y + h / 2, st);
        d.popAlpha();
        y += h + t.r(.6f);
    }
}

}
