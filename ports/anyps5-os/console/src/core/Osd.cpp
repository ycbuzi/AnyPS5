#include "core/Osd.h"
#include "core/App.h"
#include "core/Text.h"
#include <algorithm>

namespace ui {

static constexpr double kShowSeconds = 1.6;

void Osd::show(const std::string& kind, int level, bool muted) {
    if (now() >= until_ || kind != kind_) in_.restart(0, 1, .18f, easeCss);
    kind_ = kind;
    level_ = std::clamp(level, 0, 100);
    muted_ = muted;
    until_ = now() + kShowSeconds;
}

void Osd::draw(Draw& d) {
    double left = until_ - now();
    if (left <= 0) return;
    const Theme& t = app().theme;
    float k = in_.value() * (float)std::min(1.0, left / .25);
    float w = t.r(26), h = t.r(5.2f);
    Rect r{(t.W() - w) / 2, t.r(4) - t.r(1.5f) * (1 - k), w, h};
    d.pushAlpha(k);
    d.shadow(r, t.r(.9f), t.r(2), black(.5f), 0, t.r(.6f));
    d.round(r, t.r(.9f), rgba(20, 26, 46, .96f));
    std::string label = kind_ == "brightness" ? "Brightness" : muted_ ? "Muted" : "Volume";
    TextStyle st{t.r(1.3f), Weight::Regular, white(.9f), Align::Left, VAlign::Middle};
    float padX = t.r(1.6f);
    text::draw(d, label, r.x + padX, r.y + h * .32f, st);
    TextStyle num{t.r(1.3f), Weight::Regular, white(.9f), Align::Right, VAlign::Middle};
    text::draw(d, muted_ ? "" : std::to_string(level_) + "%", r.x + w - padX, r.y + h * .32f, num);
    Rect track{r.x + padX, r.y + h * .66f, w - 2 * padX, t.r(.55f)};
    d.round(track, track.h / 2, white(.18f));
    if (!muted_ && level_ > 0) d.round(Rect{track.x, track.y, track.w * level_ / 100.f, track.h}, track.h / 2, white(.95f));
    d.popAlpha();
}

}
