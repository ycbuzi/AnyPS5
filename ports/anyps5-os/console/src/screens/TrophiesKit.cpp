#include "screens/TrophiesKit.h"
#include "core/App.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/TrophyData.h"
#include "widgets/Widgets.h"
#include <cmath>
#include <unordered_map>

namespace ui::kit {

namespace {

struct Glow {
    float cx, cy, rx, ry, stop;
    Color c;
};

Color over(Color top, Color under) {
    float a = top.a;
    return {top.r * a + under.r * (1 - a), top.g * a + under.g * (1 - a), top.b * a + under.b * (1 - a), 1};
}

void paintSetbg(Draw& c, float w, float h) {
    const Glow glows[] = {
        {.13f, -.08f, .36f, .55f, .7f, rgba(255, 236, 214, .22f)},
        {.88f, .55f, .60f, .55f, .7f, rgba(36, 56, 104, .38f)},
        {.30f, 1.06f, .90f, .28f, .7f, rgba(255, 255, 255, .08f)},
    };
    Color top = hex(0x191b21), bottom = hex(0x0f1117);
    c.field({0, 0, w, h}, 120, 68, [&](float x, float y) {
        float u = x / w, v = y / h;
        Color out = mix(top, bottom, v);
        for (int i = 2; i >= 0; i--) {
            const Glow& g = glows[i];
            float dx = (u - g.cx) / g.rx, dy = (v - g.cy) / g.ry;
            float t = std::sqrt(dx * dx + dy * dy) / g.stop;
            if (t >= 1) continue;
            out = over(alpha(g.c, 1 - t), out);
        }
        return out;
    });
}

}

void setbg(Draw& d) {
    const Theme& t = app().theme;
    SDL_Texture* bg = d.cached("trophies-setbg", t.w, t.h, [&](Draw& c) { paintSetbg(c, t.W(), t.H()); });
    if (bg) d.image(bg, {0, 0, t.W(), t.H()}, Fit::Fill);
    else d.rect({0, 0, t.W(), t.H()}, hex(0x15171d));
}

void cup(Draw& d, const std::string& grade, float cx, float cy, float size) {
    Color c = grade == "none" ? white(.22f) : hex(trophies::gradeColor(grade));
    d.icon(images::icon("cup", size), cx, cy, size, c);
}

void glow(Draw& d, Rect r, float radius) {
    const Theme& t = app().theme;
    float m = t.r(2);
    const Rect bands[] = {{r.x - m, r.y - m, r.w + 2 * m, m}, {r.x - m, r.y + r.h, r.w + 2 * m, m}, {r.x - m, r.y, m, r.h}, {r.x + r.w, r.y, m, r.h}};
    for (auto& b : bands) {
        d.pushClip(b);
        widgets::focusGlow(d, r, radius);
        d.popClip();
    }
}

SDL_Texture* dimGray(Draw& d, const std::string& url, float brightness) {
    static std::unordered_map<std::string, SDL_Texture*> done;
    static unsigned gen = Draw::generation();
    if (gen != Draw::generation()) {
        done.clear();
        gen = Draw::generation();
    }
    auto it = done.find(url);
    if (it != done.end()) return it->second;
    SDL_Texture* src = images::get(url);
    if (!src) return nullptr;
    float fw = 0, fh = 0;
    SDL_GetTextureSize(src, &fw, &fh);
    int w = std::clamp((int)fw, 1, 256), h = std::clamp((int)fh, 1, 256);
    SDL_Renderer* r = d.sdl();
    SDL_Texture* tgt = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Texture* out = nullptr;
    if (tgt) {
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_Rect clip{};
        bool clipped = SDL_RenderClipEnabled(r) && SDL_GetRenderClipRect(r, &clip);
        SDL_SetRenderTarget(r, tgt);
        SDL_SetRenderClipRect(r, nullptr);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        SDL_RenderTexture(r, src, nullptr, nullptr);
        SDL_Surface* px = SDL_RenderReadPixels(r, nullptr);
        SDL_SetRenderTarget(r, prev);
        SDL_SetRenderClipRect(r, clipped ? &clip : nullptr);
        SDL_DestroyTexture(tgt);
        SDL_Surface* rgba = px ? SDL_ConvertSurface(px, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (px) SDL_DestroySurface(px);
        if (rgba) {
            for (int y = 0; y < rgba->h; y++) {
                Uint8* row = (Uint8*)rgba->pixels + y * rgba->pitch;
                for (int x = 0; x < rgba->w; x++) {
                    Uint8* p = row + x * 4;
                    float g = (.2126f * p[0] + .7152f * p[1] + .0722f * p[2]) * brightness;
                    p[0] = p[1] = p[2] = (Uint8)std::clamp(g, 0.f, 255.f);
                }
            }
            out = SDL_CreateTextureFromSurface(r, rgba);
            if (out) SDL_SetTextureScaleMode(out, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(rgba);
        }
    }
    done[url] = out;
    return out;
}

float scrollNearest(float scroll, float top, float bottom, float viewH, float contentH) {
    if (top < scroll) scroll = top;
    else if (bottom > scroll + viewH) scroll = bottom - viewH;
    float maxS = std::max(0.f, contentH - viewH);
    return std::clamp(scroll, 0.f, maxS);
}

void PsMenu::show(std::vector<MenuItem> items, Rect anchor) {
    items_ = std::move(items);
    anchor_ = anchor;
    sel_ = 0;
    open_ = true;
    in_.restart(0, 1, .17f, easeOut);
}

bool PsMenu::action(const Action& a) {
    if (!open_) return false;
    if (a.is("up") && sel_ > 0) sel_--;
    else if (a.is("down") && sel_ + 1 < (int)items_.size()) sel_++;
    else if (a.is("ok") || a.is("enter")) {
        open_ = false;
        if (sel_ < (int)items_.size() && items_[(size_t)sel_].run) items_[(size_t)sel_].run();
    } else if (a.is("back") || a.is("ps")) {
        open_ = false;
    }
    return true;
}

void PsMenu::draw(Draw& d) {
    if (!open_) return;
    const Theme& t = app().theme;
    d.rect({0, 0, t.W(), t.H()}, black(.55f));
    float mw = std::min(t.r(34.4f), t.W() - t.r(2)), mh = t.r(.5f);
    for (auto& it : items_) mh += t.r(4.5f) + (it.sep ? t.r(.7f) : 0);
    float edge = t.r(1), gap = t.r(.625f);
    float x = std::min(t.W() - mw - edge, std::max(edge, anchor_.x));
    float below = anchor_.y + anchor_.h + gap;
    float y = below + mh < t.H() ? below : std::max(edge, anchor_.y - mh - gap);
    float p = in_.value();
    y += t.r(.7f) * (1 - p);
    d.pushAlpha(p);
    Rect box{x, y, mw, mh};
    d.shadow(box, t.r(.3f), t.r(3), black(.6f), 0, t.r(1));
    d.round(box, t.r(.3f), rgba(40, 42, 50, .97f));
    pointer::target({0, 0, t.W(), t.H()}, nullptr, [this] { open_ = false; });
    pointer::target(box, nullptr, nullptr, t.r(.3f));
    float iy = y + t.r(.25f);
    TextStyle st{t.r(1.75f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    for (size_t i = 0; i < items_.size(); i++) {
        const MenuItem& it = items_[i];
        if (it.sep) {
            iy += t.r(.7f);
            d.line(x + t.r(1.2f), iy - t.r(.4f), x + mw - t.r(1.2f), iy - t.r(.4f), t.r(.06f), white(.15f));
        }
        Rect r{x, iy, mw, t.r(4.5f)};
        if ((int)i == sel_) {
            glow(d, r, t.r(.2f));
            d.round(r, t.r(.2f), white(.07f));
            d.stroke(r, t.r(.2f), t.r(.1f), white());
        }
        text::draw(d, text::ellipsize(it.label, st.font(), mw - t.r(4.2f)), x + t.r(2.1f), iy + t.r(2.25f), st);
        int n = (int)i;
        pointer::target(r, [this, n] { sel_ = n; }, [this, n] {
            sel_ = n;
            action({"ok", ""});
        }, t.r(.2f));
        iy += t.r(4.5f);
    }
    d.popAlpha();
}

}
