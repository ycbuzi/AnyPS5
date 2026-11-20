#include "widgets/osk.h"
#include "core/Anim.h"
#include "core/App.h"
#include <algorithm>
#include <cmath>

namespace ui::osk {

namespace {

const char* const PADC[] = {"1234567890@", "qwertyuiop#", "asdfghjkl'/", "zxcvbnm-_?!"};
const char* const PADS[] = {"!\"#$%&'()*+", ",-./:;<=>?@", "[\\]^_`{|}~\xE2\x82\xAC",
                            "\xC2\xA3\xC2\xA5\xE2\x80\xA2\xE2\x80\xA6\xC2\xB0\xC2\xB1\xC3\x97\xC3\xB7\xC2\xA1\xC2\xBF\xC2\xA7"};
const char* const PADA[] = {"\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA4\xC3\xA3\xC3\xA5\xC3\xA6\xC3\xA7\xC3\xA8\xC3\xA9\xC3\xAA",
                            "\xC3\xAB\xC3\xAC\xC3\xAD\xC3\xAE\xC3\xAF\xC3\xB1\xC3\xB2\xC3\xB3\xC3\xB4\xC3\xB6\xC3\xB5",
                            "\xC3\xB8\xC5\x93\xC3\xB9\xC3\xBA\xC3\xBB\xC3\xBC\xC3\xBD\xC3\xBF\xC3\x9F\xC3\xB0\xC3\xBE",
                            "\xC4\x81\xC4\x93\xC4\xAB\xC5\x8D\xC5\xAB\xC5\x82\xC5\x9B\xC5\xBA\xC5\xBC\xC4\x9F\xC5\x9F"};

struct Fn {
    const char* k;
    const char* label;
    const char* badge;
    int w;
};

const std::vector<std::vector<Fn>>& fnRows() {
    static const std::vector<std::vector<Fn>> rows = {
        {{"Shift", "\xE2\x87\xA7", "L2", 2}, {"ABC", "ABC", "", 1}, {"Sym", "@#:", "", 1}, {"Acc", "\xC3\xA0\xC3\xA9", "", 1},
         {"Space", "Space", "\xE2\x96\xB3", 4}, {"\xE2\x8C\xAB", "\xE2\x8C\xAB", "\xE2\x96\xA1", 2}},
        {{"Left", "\xE2\x97\x80", "L1", 2}, {"Right", "\xE2\x96\xB6", "R1", 2}, {"Done", "Done", "R2", 7}}};
    return rows;
}

}

std::vector<Key> Keyboard::layout() const {
    std::vector<Key> out;
    const char* const* rows = page == "sym" ? PADS : page == "acc" ? PADA : PADC;
    for (int ri = 0; ri < 4; ri++) {
        std::u32string r = decode(rows[ri]);
        for (int ci = 0; ci < (int)r.size(); ci++) out.push_back({encode(r[(size_t)ci]), "", "", ri, ci, 1});
    }
    int fi = 0;
    for (auto& row : fnRows()) {
        int c = 0;
        for (auto& f : row) {
            out.push_back({f.k, f.label, f.badge, 4 + fi, c, f.w});
            c += f.w;
        }
        fi++;
    }
    return out;
}

std::string Keyboard::keyAt(int i) const {
    auto l = layout();
    return i >= 0 && i < (int)l.size() ? l[(size_t)i].k : std::string();
}

int Keyboard::move(int i, const std::string& dir) const {
    auto lay = layout();
    if (i < 0 || i >= (int)lay.size()) return 0;
    const Key& x = lay[(size_t)i];
    if (dir == "left" || dir == "right") {
        int j = dir == "left" ? i - 1 : i + 1;
        return j >= 0 && j < (int)lay.size() && lay[(size_t)j].r == x.r ? j : i;
    }
    int r = x.r + (dir == "up" ? -1 : dir == "down" ? 1 : 0);
    if (r == x.r) return i;
    int last = -1, hit = -1;
    for (int j = 0; j < (int)lay.size(); j++) {
        if (lay[(size_t)j].r != r) continue;
        last = j;
        if (hit < 0 && x.c >= lay[(size_t)j].c && x.c < lay[(size_t)j].c + lay[(size_t)j].w) hit = j;
    }
    if (last < 0) return i;
    return hit >= 0 ? hit : last;
}

void Keyboard::edit(Field& f, const std::string& k, int max, bool capFirst) {
    if (k == "\xE2\x8C\xAB") {
        if (f.cur > 0) {
            f.text.erase((size_t)f.cur - 1, 1);
            f.cur--;
        }
    } else if (k == "Left") f.cur = std::max(0, f.cur - 1);
    else if (k == "Right") f.cur = std::min((int)f.text.size(), f.cur + 1);
    else if (k == "Shift") caps = !caps;
    else if (k == "ABC") page = "abc";
    else if (k == "Sym") page = "sym";
    else if (k == "Acc") page = "acc";
    else if ((int)f.text.size() < max) {
        std::u32string kk = decode(k);
        std::u32string ch;
        if (k == "Space") ch = U" ";
        else if (kk.size() == 1 && page != "sym") ch = (caps || (capFirst && f.text.empty())) ? decode(upper(k)) : kk;
        else ch = kk;
        f.text.insert((size_t)f.cur, ch);
        f.cur += (int)ch.size();
    }
}

std::string Keyboard::keyLabel(const Key& k) const {
    if (!k.label.empty()) return k.label;
    return page != "sym" && caps ? upper(k.k) : k.k;
}

float Keyboard::height() const {
    const Theme& t = app().theme;
    return t.r(.9f + 4 * 4 + .5f + 3.4f + .1f + 3.4f + .4f);
}

void Keyboard::draw(Draw& d, float x, float y, int sel, bool focused) const {
    const Theme& t = app().theme;
    float w = t.r(54.4f), padX = t.r(.4f), colW = (w - 2 * padX) / 11;
    d.round({x, y, w, height()}, t.r(.2f), rgba(28, 30, 38, .94f));
    float rowTop[6];
    rowTop[0] = y + t.r(.9f);
    for (int i = 1; i <= 4; i++) rowTop[i] = rowTop[i - 1] + t.r(4);
    rowTop[5] = rowTop[4] + t.r(3.9f);
    auto lay = layout();
    for (int i = 0; i < (int)lay.size(); i++) {
        const Key& k = lay[(size_t)i];
        bool on = focused && i == sel;
        float cx0 = x + padX + k.c * colW;
        if (k.label.empty()) {
            Rect cell{cx0, rowTop[k.r], colW, t.r(4)};
            if (on) {
                d.round(cell, t.r(.2f), white(.14f));
                d.outline(cell, t.r(.2f), t.r(.1f), white(.85f));
            }
            text::draw(d, keyLabel(k), cell.x + cell.w / 2, cell.y + cell.h / 2, {t.r(1.65f), Weight::Light, white(), Align::Center, VAlign::Middle});
            continue;
        }
        float top = rowTop[k.r] + (k.r == 4 ? t.r(.5f) : t.r(.1f));
        Rect cell{cx0 + t.r(.05f), top, colW * k.w - t.r(.1f), t.r(3.4f)};
        bool act = (k.k == "Shift" && caps) || (k.k == "ABC" && page == "abc") || (k.k == "Sym" && page == "sym") || (k.k == "Acc" && page == "acc");
        d.round(cell, t.r(.2f), white(act ? .22f : .07f));
        if (on) d.outline(cell, t.r(.2f), t.r(.1f), white(.85f));
        text::draw(d, k.label, cell.x + cell.w / 2, cell.y + cell.h / 2, {t.r(1.4f), Weight::Regular, white(), Align::Center, VAlign::Middle});
        if (!k.badge.empty())
            text::draw(d, k.badge, cell.x + cell.w - t.r(.3f), cell.y + t.r(.15f), {t.r(1.25f), Weight::Regular, white(.55f), Align::Right, VAlign::Top});
    }
}

float caretAlpha() {
    double p = std::fmod(now(), 1.0);
    float k = (float)(p < .5 ? p * 2 : (1 - p) * 2);
    return .2f + .8f * easeCss(k);
}

void caretText(Draw& d, const std::string& shown, int cur, float x, float cy, const TextStyle& st, float caretH) {
    const Theme& t = app().theme;
    std::u32string u = decode(shown);
    cur = std::clamp(cur, 0, (int)u.size());
    std::string a = encode(u.substr(0, (size_t)cur)), b = encode(u.substr((size_t)cur));
    float wa = a.empty() ? 0 : text::draw(d, a, x, cy, st);
    float cx = x + wa + t.r(.05f);
    d.rect({cx, cy - caretH / 2, t.r(.12f), caretH}, white(caretAlpha()));
    if (!b.empty()) text::draw(d, b, cx + t.r(.17f), cy, st);
}

static Color over(Color base, Color top) {
    float a = top.a;
    return {base.r + (top.r - base.r) * a, base.g + (top.g - base.g) * a, base.b + (top.b - base.b) * a, 1};
}

static float radial(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    float r = std::sqrt(dx * dx + dy * dy);
    return std::max(0.f, 1 - r / .7f);
}

static void paintSetBg(Draw& c, float W, float H) {
    c.field({0, 0, W, H}, 96, 54, [W, H](float x, float y) {
        Color base = mix(hex(0x191b21), hex(0x0f1117), y / H);
        base = over(base, rgba(255, 255, 255, .08f * radial(x, y, .30f * W, 1.06f * H, .90f * W, .28f * H)));
        base = over(base, rgba(36, 56, 104, .38f * radial(x, y, .88f * W, .55f * H, .60f * W, .55f * H)));
        base = over(base, rgba(255, 236, 214, .22f * radial(x, y, .13f * W, -.08f * H, .36f * W, .55f * H)));
        return base;
    });
}

void pageBackground(Draw& d) {
    const Theme& t = app().theme;
    SDL_Texture* bg = d.cached("osk-setbg", t.w, t.h, [&](Draw& c) { paintSetBg(c, t.W(), t.H()); });
    d.image(bg, {0, 0, t.W(), t.H()}, Fit::Fill);
}

}
