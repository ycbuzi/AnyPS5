#include "screens/settings_draw.h"
#include "screens/settings_a11yfx.h"
#include "core/Pointer.h"
#include <algorithm>
#include <cmath>

namespace ui::sett {

static Color over(Color base, Color top) {
    float a = top.a;
    return {base.r * (1 - a) + top.r * a, base.g * (1 - a) + top.g * a, base.b * (1 - a) + top.b * a, 1};
}

static float ellipse(float x, float y, float cx, float cy, float rx, float ry, float stop) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    float d = std::sqrt(dx * dx + dy * dy);
    return std::max(0.f, 1 - d / stop);
}

void paintSettingsBg(Draw& c, float W, float H) {
    Color top = hex(0x191b21), bot = hex(0x0f1117);
    Color warm = rgba(255, 236, 214), blue = rgba(36, 56, 104), haze = white();
    c.field({0, 0, W, H}, 120, 68, [&](float x, float y) {
        Color col = mix(top, bot, y / H);
        col = over(col, alpha(haze, .08f * ellipse(x, y, W * .30f, H * 1.06f, W * .90f, H * .28f, .7f)));
        col = over(col, alpha(blue, .38f * ellipse(x, y, W * .88f, H * .55f, W * .60f, H * .55f, .7f)));
        col = over(col, alpha(warm, .22f * ellipse(x, y, W * .13f, -H * .08f, W * .36f, H * .55f, .7f)));
        return col;
    });
}

ListGeom listGeom(bool top) {
    const Theme& t = app().theme;
    ListGeom g;
    g.top = top;
    if (top) {
        float w = std::min(t.r(82.75f), t.W() - t.r(6));
        g.box = {(t.W() - w) / 2, t.r(11.1f), w, 0};
    } else {
        g.box = {t.r(42), t.r(11.5f), std::min(t.r(67), t.W() - t.r(42) - t.r(1)), 0};
    }
    g.box.h = t.H() - t.r(4.2f) - g.box.y;
    return g;
}

void selBox(Draw& d, Rect r, float radius) {
    const Theme& t = app().theme;
    d.round(r, radius, white(.07f));
    d.outline(r, radius, t.r(.1f), white(.85f));
    if (t.highContrast) d.stroke(r, radius, t.r(.22f), white());
}

void drawToggle(Draw& d, float right, float cy, bool on) {
    const Theme& t = app().theme;
    Rect r{right - t.r(3.9f), cy - t.r(1.85f) / 2, t.r(3.9f), t.r(1.85f)};
    Color fill = on ? hex(0x1f6fe5) : white(.18f);
    Color edge = on ? hex(0x1f6fe5) : white(.35f);
    d.round(r, t.r(1), fill);
    d.stroke(r, t.r(1), t.r(.08f), edge);
    float b = t.r(.08f), k = t.r(1.25f);
    float kx = r.x + b + (on ? t.r(2.3f) : t.r(.25f));
    d.circle(kx + k / 2, cy, k / 2, on ? white() : white(.85f));
}

void drawBars(Draw& d, float x, float cy, int signal, Color c) {
    const Theme& t = app().theme;
    int lvl = signal < 0 ? 0 : signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 25 ? 2 : 1;
    float sx = t.r(1.7f) / 19.f, sy = t.r(1.4f) / 16.f, y0 = cy - t.r(1.4f) / 2;
    for (int i = 0; i < 4; i++) {
        Rect b{x + i * 5 * sx, y0 + (12 - i * 4) * sy, 3.4f * sx, (4 + i * 4) * sy};
        d.round(b, .8f * sx, alpha(c, i < lvl ? 1.f : .25f));
    }
}

float spacedText(Draw& d, const std::string& s, float x, float y, const TextStyle& st, float spacing) {
    float cx = x;
    size_t i = 0;
    while (i < s.size()) {
        size_t n = 1;
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 0xF0) n = 4;
        else if (ch >= 0xE0) n = 3;
        else if (ch >= 0xC0) n = 2;
        std::string g = s.substr(i, n);
        TextStyle one = st;
        one.align = Align::Left;
        cx += text::draw(d, g, cx, y, one) + spacing;
        i += n;
    }
    return cx - x;
}

struct Layout {
    float y, h;
    std::vector<std::string> lines;
};

static bool safeMode() { return app().settings.get("safe", false) == true; }

static float valueWidth(const Row& r, Font vf, float maxText, std::string& shown) {
    const Theme& t = app().theme;
    float w = 0;
    if (r.toggle) return t.r(3.9f);
    if (r.swatch >= 0) w += t.r(1.5f);
    if (r.lock) w += t.r(1.8f);
    if (r.bars != -1) w += t.r(1.7f);
    if (!r.val.empty()) {
        shown = text::ellipsize(r.val, vf, maxText);
        w += text::width(shown, vf);
        if (r.bars != -1) w += t.r(.6f);
    }
    if (!r.valIcon.empty()) w += t.r(1.6f) + t.r(.6f);
    return w;
}

static void drawValue(Draw& d, const Row& r, float right, float cy, Font vf, const std::string& shown) {
    const Theme& t = app().theme;
    if (r.toggle) {
        drawToggle(d, right, cy, r.toggle());
        return;
    }
    Color c = r.info ? white() : white(.6f);
    if (r.dim) c = white(r.info ? .6f : .36f);
    if (r.bad) c = hex(0xff6b6b);
    float x = right;
    if (r.swatch >= 0) {
        auto& cols = avatarColors();
        auto& pr = cols[r.swatch % cols.size()];
        float s = t.r(1.5f);
        d.roundGradient({x - s, cy - s / 2, s, s}, s / 2, 135, pr.first, pr.second);
        x -= s;
    }
    if (r.lock) {
        float s = t.r(1.3f);
        if (SDL_Texture* ic = images::icon("set-lock", s)) d.icon(ic, x - s / 2, cy, s, c);
        x -= s + t.r(.5f);
    }
    auto bars = [&] {
        drawBars(d, x - t.r(1.7f), cy, r.bars, c);
        x -= t.r(1.7f);
    };
    auto label = [&] {
        float w = text::width(shown, vf);
        if (r.secret && safeMode()) d.round({x - w, cy - vf.size * .38f, w, vf.size * .76f}, vf.size * .38f, white(.2f));
        else text::draw(d, shown, x, cy, {vf.size, vf.weight, c, Align::Right, VAlign::Middle});
        x -= w;
    };
    if (r.bars != -1 && r.barsLeft) {
        if (!shown.empty()) {
            label();
            x -= t.r(.6f);
        }
        bars();
    } else {
        if (r.bars != -1) {
            bars();
            if (!shown.empty()) x -= t.r(.6f);
        }
        if (!shown.empty()) label();
    }
    if (!r.valIcon.empty()) {
        float s = t.r(1.6f);
        x -= t.r(.6f);
        if (SDL_Texture* ic = images::icon(r.valIcon, s)) d.icon(ic, x - s / 2, cy, s, c);
    }
}

void drawList(Draw& d, const Rows& R, const ListGeom& g, int sel, float& scroll, Rect& anchorOut,
    std::function<void(int, bool)> onRow, std::function<void(float)> onScroll) {
    const Theme& t = app().theme;
    float cx = g.box.x + t.r(.4f), cw = g.box.w - t.r(.8f);
    float rowH = t.r(6.1f);
    Font lf{tsz(1.9f), Weight::Regular}, vf{tsz(1.5f), Weight::Regular};
    Font hf{tsz(1.3f), Weight::Regular}, nf{tsz(1.35f), Weight::Regular};
    std::vector<Layout> L(R.size());
    float y = t.r(.3f);
    for (size_t i = 0; i < R.size(); i++) {
        const Row& r = R[i];
        if (r.head) {
            y += t.r(1.6f);
            L[i] = {y, text::lineHeight(hf), {}};
            y += L[i].h + t.r(.4f);
        } else if (r.note) {
            y += t.r(1);
            auto lines = text::wrap(r.label, nf, cw - t.r(2));
            L[i] = {y, nf.size * 1.55f * lines.size(), lines};
            y += L[i].h;
        } else {
            L[i] = {y, rowH, {}};
            y += rowH;
        }
    }
    float total = y + t.r(.8f), viewH = g.box.h;
    if (sel >= 0 && sel < (int)R.size()) {
        int first = -1;
        for (size_t i = 0; i < R.size(); i++)
            if (R[i].selectable()) {
                first = (int)i;
                break;
            }
        if (sel == first) scroll = 0;
        float top = L[sel].y, bot = L[sel].y + L[sel].h;
        if (top - scroll < 0) scroll = top;
        if (bot - scroll > viewH) scroll = bot - viewH;
    } else {
        scroll = 0;
    }
    scroll = std::max(0.f, std::min(scroll, std::max(0.f, total - viewH)));
    d.pushClip({g.box.x - t.r(1.5f), g.box.y, g.box.w + t.r(3), g.box.h});
    if (onScroll) pointer::scrollArea(g.box, onScroll);
    float pad = g.top ? t.r(1.4f) : t.r(1), gap = g.top ? t.r(2.2f) : t.r(1.6f);
    for (size_t i = 0; i < R.size(); i++) {
        const Row& r = R[i];
        float ry = g.box.y + L[i].y - scroll;
        if (ry > g.box.y + viewH || ry + L[i].h < g.box.y) continue;
        if (r.head) {
            float sp = hf.size * .08f;
            spacedText(d, text::upper(r.label), cx + t.r(1), ry, {hf.size, hf.weight, white(.5f), Align::Left, VAlign::Top}, sp);
            continue;
        }
        if (r.note) {
            text::drawLines(d, L[i].lines, cx + t.r(1), ry, nf.size * 1.55f, {nf.size, nf.weight, white(.65f), Align::Left, VAlign::Top});
            continue;
        }
        Rect rr{cx, ry, cw, rowH};
        if (onRow && r.selectable()) pointer::target(rr, [onRow, i] { onRow((int)i, false); }, [onRow, i] { onRow((int)i, true); }, t.r(.25f));
        float cy = ry + rowH / 2;
        d.rect({rr.x, rr.y + rr.h - t.r(.06f), rr.w, t.r(.06f)}, white(.09f));
        if ((int)i == sel) {
            selBox(d, rr, t.r(.25f));
            anchorOut = rr;
            a11yfx::focus(rr);
        }
        float x = rr.x + pad, right = rr.x + rr.w - pad;
        if (!r.icon.empty()) {
            float s = t.r(2.3f);
            if (SDL_Texture* ic = images::icon(r.icon, s)) d.icon(ic, x + t.r(2.4f) / 2, cy, s, white(.75f));
            x += t.r(2.4f) + gap;
        }
        std::string shown;
        float vw = valueWidth(r, vf, (rr.w - 2 * pad) * .48f, shown);
        float labelMax = right - x - (vw > 0 ? vw + gap : 0);
        std::string lab = text::ellipsize(r.label, lf, std::max(0.f, labelMax));
        text::draw(d, lab, x, cy, {lf.size, lf.weight, white(), Align::Left, VAlign::Middle});
        drawValue(d, r, right, cy, vf, shown);
    }
    d.popClip();
}

}
