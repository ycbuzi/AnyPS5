#include "core/Anim.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/settings.h"
#include "screens/settings_a11yfx.h"
#include <algorithm>
#include <cmath>

namespace ui::sett {

struct OskKey {
    std::string k, label, badge;
    int r = 0, c = 0, w = 1;
};

static std::vector<std::string> cps(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char ch = (unsigned char)s[i];
        size_t n = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

static std::string join(const std::vector<std::string>& v, size_t a, size_t b) {
    std::string s;
    for (size_t i = a; i < b && i < v.size(); i++) s += v[i];
    return s;
}

static std::string upperCp(const std::string& c) {
    if (c.size() == 1) return std::string(1, (char)std::toupper((unsigned char)c[0]));
    return text::upper(c);
}

static std::string pendTitle, pendText;
static std::function<void(const std::string&)> pendDone;
static OskOpts pendOpts;
static int oskGen = 0;

class SettingsOsk : public Screen {
public:
    bool overlay() const override { return true; }
    bool typing() const override { return true; }

    void enter() override { take(); }
    void update(double) override {
        if (gen_ != oskGen) take();
    }

    std::vector<Hint> legend() const override { return {{"Done", "r2"}, {"Back", "back"}}; }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        if (k == "text") {
            for (auto& c : cps(a.text)) insert(c, false);
            return true;
        }
        if (k == "space" || k == "triangle") press("Space");
        else if (k == "square") press("\xE2\x8C\xAB");
        else if (k == "l1") press("Left");
        else if (k == "r1") press("Right");
        else if (k == "l2") press("Shift");
        else if (k == "r2" || k == "enter") press("Done");
        else if (k == "ok") {
            auto lay = layout();
            if (sel_ >= 0 && sel_ < (int)lay.size()) press(lay[sel_].k);
        } else if (k == "back") app().router.close(id);
        else if (k == "up" || k == "down" || k == "left" || k == "right") sel_ = moveSel(sel_, k);
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, rgba(3, 6, 16, .86f));
        Font tf{tsz(1.6f), Weight::Light}, ff{tsz(1.4f), Weight::Regular};
        float gridW = std::min(t.r(54.4f), t.W() - t.r(4));
        float keyH = t.r(4), fnH = t.r(3.4f);
        auto lay = layout();
        int rows = 0;
        for (auto& k : lay) rows = std::max(rows, k.r + 1);
        int base = rows - 2;
        float gridH = t.r(.9f) + base * keyH + t.r(.5f) + fnH + t.r(.1f) + fnH + t.r(.4f);
        float titleH = text::lineHeight(tf);
        float total = titleH + t.r(1) + t.r(3.4f) + t.r(1.4f) + gridH;
        float y = (t.H() - total) / 2, cx = t.W() / 2;
        text::draw(d, title_, cx, y, {tf.size, tf.weight, white(), Align::Center, VAlign::Top});
        y += titleH + t.r(1);
        std::string shown;
        auto v = cps(text_);
        for (size_t i = 0; i < v.size(); i++) shown += opts_.password ? "\xE2\x80\xA2" : v[i];
        std::string before = opts_.password ? std::string() : join(v, 0, cur_);
        if (opts_.password) for (int i = 0; i < cur_; i++) before += "\xE2\x80\xA2";
        float fw = std::min(t.r(40), std::max(t.r(34), text::width(shown, ff) + t.r(2.4f) + t.r(.4f)));
        Rect field{cx - fw / 2, y, fw, t.r(3.4f)};
        d.round(field, t.r(.6f), white(.1f));
        float tx = field.x + t.r(1.2f), fcy = field.y + field.h / 2;
        {
            std::vector<float> edges{0};
            std::string pre;
            for (auto& c : v) {
                pre += opts_.password ? "\xE2\x80\xA2" : c;
                edges.push_back(text::width(pre, ff));
            }
            pointer::target(field, nullptr, [this, edges, tx] {
                float px = pointer::x() - tx;
                int best = 0;
                for (size_t i = 1; i < edges.size(); i++)
                    if (std::fabs(edges[i] - px) < std::fabs(edges[best] - px)) best = (int)i;
                cur_ = best;
            }, t.r(.6f));
        }
        d.pushClip(field);
        text::draw(d, shown, tx, fcy, {ff.size, ff.weight, white(), Align::Left, VAlign::Middle});
        float caretX = tx + text::width(before, ff);
        float blink = 0.5f + 0.5f * std::cos((float)now() * 6.2832f);
        d.rect({caretX, fcy - t.r(.8f), t.r(.12f), t.r(1.6f)}, white(blink));
        d.popClip();
        y += t.r(3.4f) + t.r(1.4f);
        Rect grid{cx - gridW / 2, y, gridW, gridH};
        d.round(grid, t.r(.2f), rgba(28, 30, 38, .94f));
        float colW = (gridW - t.r(.8f)) / 11.f;
        Font kf{tsz(1.65f), Weight::Light}, fnf{tsz(1.4f), Weight::Regular}, bf{tsz(1.25f), Weight::Regular};
        for (size_t i = 0; i < lay.size(); i++) {
            const OskKey& k = lay[i];
            bool fn = !k.label.empty();
            float kx = grid.x + t.r(.4f) + k.c * colW;
            Rect kr;
            if (!fn) kr = {kx, grid.y + t.r(.9f) + k.r * keyH, colW, keyH};
            else {
                float ky = grid.y + t.r(.9f) + base * keyH + t.r(.5f) + (k.r - base) * (fnH + t.r(.1f));
                kr = {kx + t.r(.05f), ky, k.w * colW - t.r(.1f), fnH};
                bool act = (k.k == "Shift" && caps_) || (k.k == "ABC" && page_ == "abc") || (k.k == "Sym" && page_ == "sym") || (k.k == "Acc" && page_ == "acc");
                d.round(kr, t.r(.2f), white(act ? .22f : .07f));
            }
            pointer::target(kr, [this, i] { sel_ = (int)i; }, [this, i, key = k.k] {
                sel_ = (int)i;
                press(key);
            }, t.r(.2f));
            if ((int)i == sel_) {
                a11yfx::focus(kr);
                d.round(kr, t.r(.2f), white(.14f));
                d.outline(kr, t.r(.2f), t.r(.1f), white(.85f));
            }
            std::string lab = fn ? k.label : (page_ != "sym" && caps_ ? upperCp(k.k) : k.k);
            Font f = fn ? fnf : kf;
            text::draw(d, lab, kr.x + kr.w / 2, kr.y + kr.h / 2, {f.size, f.weight, white(), Align::Center, VAlign::Middle});
            if (!k.badge.empty()) text::draw(d, k.badge, kr.x + kr.w - t.r(.3f), kr.y + t.r(.15f), {bf.size, bf.weight, white(.55f), Align::Right, VAlign::Top});
        }
    }

private:
    void take() {
        title_ = pendTitle;
        text_ = pendText;
        done_ = pendDone;
        opts_ = pendOpts;
        cur_ = (int)cps(text_).size();
        page_ = "abc";
        caps_ = false;
        sel_ = 0;
        gen_ = oskGen;
    }

    std::vector<OskKey> layout() const {
        static const std::vector<std::string> C = {"1234567890@", "qwertyuiop#", "asdfghjkl'/", "zxcvbnm-_?!"};
        static const std::vector<std::string> S = {"!\"#$%&'()*+", ",-./:;<=>?@", "[\\]^_`{|}~\xE2\x82\xAC",
            "\xC2\xA3\xC2\xA5\xE2\x80\xA2\xE2\x80\xA6\xC2\xB0\xC2\xB1\xC3\x97\xC3\xB7\xC2\xA1\xC2\xBF\xC2\xA7"};
        static const std::vector<std::string> A = {"\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA4\xC3\xA3\xC3\xA5\xC3\xA6\xC3\xA7\xC3\xA8\xC3\xA9\xC3\xAA",
            "\xC3\xAB\xC3\xAC\xC3\xAD\xC3\xAE\xC3\xAF\xC3\xB1\xC3\xB2\xC3\xB3\xC3\xB4\xC3\xB6\xC3\xB5",
            "\xC3\xB8\xC5\x93\xC3\xB9\xC3\xBA\xC3\xBB\xC3\xBC\xC3\xBD\xC3\xBF\xC3\x9F\xC3\xB0\xC3\xBE",
            "\xC4\x81\xC4\x93\xC4\xAB\xC5\x8D\xC5\xAB\xC5\x82\xC5\x9B\xC5\xBA\xC5\xBC\xC4\x9F\xC5\x9F"};
        const auto& rows = page_ == "sym" ? S : page_ == "acc" ? A : C;
        std::vector<OskKey> out;
        for (size_t r = 0; r < rows.size(); r++) {
            auto v = cps(rows[r]);
            for (size_t c = 0; c < v.size(); c++) out.push_back({v[c], "", "", (int)r, (int)c, 1});
        }
        int b = (int)rows.size();
        struct F { const char *k, *l, *badge; int w; };
        static const std::vector<std::vector<F>> fn = {
            {{"Shift", "\xE2\x87\xA7", "L2", 2}, {"ABC", "ABC", "", 1}, {"Sym", "@#:", "", 1}, {"Acc", "\xC3\xA0\xC3\xA9", "", 1},
                {"Space", "Space", "\xE2\x96\xB3", 4}, {"\xE2\x8C\xAB", "\xE2\x8C\xAB", "\xE2\x96\xA1", 2}},
            {{"Left", "\xE2\x97\x80", "L1", 2}, {"Right", "\xE2\x96\xB6", "R1", 2}, {"Done", "Done", "R2", 7}}};
        for (size_t fi = 0; fi < fn.size(); fi++) {
            int c = 0;
            for (auto& f : fn[fi]) {
                out.push_back({f.k, f.l, f.badge, b + (int)fi, c, f.w});
                c += f.w;
            }
        }
        return out;
    }

    int moveSel(int i, const std::string& k) const {
        auto lay = layout();
        if (i < 0 || i >= (int)lay.size()) return 0;
        const OskKey& x = lay[i];
        if (k == "left" || k == "right") {
            int j = k == "left" ? i - 1 : i + 1;
            return j >= 0 && j < (int)lay.size() && lay[j].r == x.r ? j : i;
        }
        int r = x.r + (k == "up" ? -1 : 1);
        int last = -1;
        for (size_t j = 0; j < lay.size(); j++) {
            if (lay[j].r != r) continue;
            last = (int)j;
            if (x.c >= lay[j].c && x.c < lay[j].c + lay[j].w) return (int)j;
        }
        return last >= 0 ? last : i;
    }

    void insert(const std::string& c, bool fromGrid) {
        auto v = cps(text_);
        if ((int)v.size() >= opts_.max) return;
        std::string ch = c;
        if (fromGrid && ch.size() >= 1 && page_ != "sym" && (caps_ || (opts_.capFirst && v.empty()))) ch = upperCp(ch);
        else if (!fromGrid && opts_.capFirst && v.empty() && page_ != "sym") ch = upperCp(ch);
        v.insert(v.begin() + cur_, ch);
        cur_++;
        text_ = join(v, 0, v.size());
    }

    void press(const std::string& k) {
        auto v = cps(text_);
        if (k == "Done") {
            auto done = done_;
            std::string out = text_;
            app().router.close(id);
            if (done) done(out);
            return;
        }
        if (k == "\xE2\x8C\xAB") {
            if (cur_ > 0) {
                v.erase(v.begin() + cur_ - 1);
                cur_--;
                text_ = join(v, 0, v.size());
            }
        } else if (k == "Left") cur_ = std::max(0, cur_ - 1);
        else if (k == "Right") cur_ = std::min((int)v.size(), cur_ + 1);
        else if (k == "Shift") caps_ = !caps_;
        else if (k == "ABC") page_ = "abc";
        else if (k == "Sym") page_ = "sym";
        else if (k == "Acc") page_ = "acc";
        else if (k == "Space") insert(" ", false);
        else insert(k, true);
    }

    std::string title_, text_, page_ = "abc";
    std::function<void(const std::string&)> done_;
    OskOpts opts_;
    int cur_ = 0, sel_ = 0, gen_ = 0;
    bool caps_ = false;
};

ANYPS5_SCREEN(SettingsOsk, "settingsosk");

void osk(const std::string& title, const std::string& text, std::function<void(const std::string&)> done, OskOpts o) {
    pendTitle = title;
    pendText = text;
    pendDone = std::move(done);
    pendOpts = o;
    oskGen++;
    if (!app().router.isOpen("settingsosk")) app().open("settingsosk");
}

}
