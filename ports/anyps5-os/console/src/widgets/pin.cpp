#include "core/Glyphs.h"
#include "widgets/pin.h"
#include "core/App.h"
#include "widgets/Widgets.h"
#include "widgets/osk.h"

namespace ui {

namespace {

const char* const KP[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "\xE2\x8C\xAB"};
const char* const KPB[] = {"\xE2\x97\x80", "\xE2\x96\xB2", "\xE2\x96\xB6", "\xE2\x96\xBC", "R1", "R2", "L1", "L2", "\xE2\x96\xB3", "", "\xE2\x96\xA1", "Bksp"};
const char* const BKSP = "\xE2\x8C\xAB";

struct Request {
    std::string mode, then;
    json user;
    std::function<void()> changed;
};

Request& request() {
    static Request r;
    return r;
}

std::string padLabel(int i) {
    static const char* const acts[] = {"", "", "", "", "r1", "r2", "l1", "l2", "triangle", "", "square", ""};
    Source s = glyph::pad();
    if (!*acts[i] || s == Source::PlayStation) return KPB[i];
    if (s == Source::Generic) return i == 8 ? "Top" : i == 10 ? "Left" : KPB[i];
    return glyph::name(acts[i], s);
}

const char* padDigit(const std::string& k) {
    static const std::pair<const char*, const char*> m[] = {{"left", "1"}, {"up", "2"}, {"right", "3"}, {"down", "4"}, {"r1", "5"},
                                                           {"r2", "6"}, {"l1", "7"}, {"l2", "8"}, {"triangle", "9"}, {"square", "0"}};
    for (auto& p : m)
        if (k == p.first) return p.second;
    return nullptr;
}

}

class Passcode : public Screen {
public:
    bool overlay() const override { return true; }
    bool typing() const override { return true; }

    void enter() override {
        Request& r = request();
        then_ = r.then;
        changed_ = std::move(r.changed);
        r.changed = nullptr;
        std::string mode = r.mode.empty() ? "login" : r.mode;
        user_ = mode == "login" ? app().login : r.user;
        r.mode.clear();
        r.then.clear();
        shakeAt_ = -10;
        start(mode);
    }

    void update(double) override {
        if (doneAt_ > 0 && now() >= doneAt_) {
            doneAt_ = 0;
            finish();
        }
    }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        if (k == "back") {
            app().router.close(id);
            return true;
        }
        if (a.pad) {
            if (const char* dg = padDigit(k)) digit(dg);
            return true;
        }
        if (k == "square") press(BKSP);
        else if (k == "text") {
            for (char c : a.text)
                if (c >= '0' && c <= '9') digit(std::string(1, c));
        } else if (k.size() == 1 && k[0] >= '0' && k[0] <= '9') digit(k);
        else if (k == "ok" || k == "enter") {
            if (*KP[sel_]) press(KP[sel_]);
        } else {
            int was = sel_;
            if (k == "left" && sel_ % 3) sel_--;
            else if (k == "right" && sel_ % 3 < 2) sel_++;
            else if (k == "up" && sel_ > 2) sel_ -= 3;
            else if (k == "down" && sel_ < 9) sel_ += 3;
            if (!*KP[sel_]) sel_ = was;
        }
        return true;
    }

    void draw(Draw& d) override;

private:
    void start(const std::string& mode) {
        mode_ = mode;
        digits_.clear();
        sel_ = 0;
        doneAt_ = 0;
        bad_ = false;
        prompt_ = mode == "login" ? "Enter your passcode." : mode == "set1" ? "Enter a new passcode." : mode == "set2" ? "Enter the new passcode again." : "Enter the current passcode.";
    }

    std::string title() const { return mode_ == "login" ? "Log In" : mode_ == "check" ? "Passcode" : "Set Login Passcode"; }

    void digit(const std::string& dg) {
        if (digits_.size() >= 4) return;
        digits_ += dg;
        if (digits_.size() == 4) doneAt_ = now() + .2;
    }

    void press(const std::string& k) {
        if (k == BKSP) {
            if (!digits_.empty()) digits_.pop_back();
            doneAt_ = 0;
        } else digit(k);
    }

    void wrong(const std::string& msg) {
        shakeAt_ = now();
        prompt_ = msg;
        bad_ = true;
        digits_.clear();
    }

    std::string name() const { return user_.is_object() ? user_.value("name", std::string()) : std::string(); }

    json* stored() {
        auto& s = app().settings;
        std::string uid = user_.is_object() ? user_.value("uid", std::string()) : std::string();
        json* p = uid.empty() ? nullptr : s.profileByUid(uid);
        return p ? p : s.profileByName(name());
    }

    void storeHash(const std::string& h) {
        App& A = app();
        if (json* p = stored()) (*p)["pinHash"] = h;
        user_["pinHash"] = h;
        if (A.me.is_object() && A.me.value("name", std::string()) == name()) A.me["pinHash"] = h;
        A.saveSettings();
        A.applySettings();
        if (changed_) changed_();
    }

    void finish() {
        if (digits_.size() != 4 || !user_.is_object()) return;
        App& A = app();
        bool match = pin::hash(name(), digits_) == user_.value("pinHash", std::string());
        if (mode_ == "login") {
            if (!match) return wrong("The passcode is incorrect.");
            json target = user_;
            A.router.close(id);
            A.enterHome(target);
        } else if (mode_ == "set1") {
            first_ = digits_;
            start("set2");
        } else if (mode_ == "set2") {
            if (digits_ != first_) {
                mode_ = "set1";
                return wrong("They did not match. Enter a new passcode.");
            }
            A.router.close(id);
            storeHash(pin::hash(name(), digits_));
            A.toast("Passcode set for " + name());
        } else if (mode_ == "check") {
            if (!match) return wrong("The passcode is incorrect.");
            if (then_ == "remove") {
                A.router.close(id);
                storeHash("");
                A.toast("Passcode removed");
            } else start("set1");
        }
    }

    float shake() const;

    std::string mode_, then_, digits_, first_, prompt_;
    json user_;
    std::function<void()> changed_;
    int sel_ = 0;
    double doneAt_ = 0, shakeAt_ = -10;
    bool bad_ = false;
};

float Passcode::shake() const {
    float p = (float)((now() - shakeAt_) / .4);
    if (p <= 0 || p >= 1) return 0;
    if (p < .25f) return -.8f * easeCss(p / .25f);
    if (p < .75f) return -.8f + 1.6f * easeCss((p - .25f) / .5f);
    return .8f - .8f * easeCss((p - .75f) / .25f);
}

void Passcode::draw(Draw& d) {
    const Theme& t = app().theme;
    d.rect({0, 0, t.W(), t.H()}, hex(0x15171d));
    osk::pageBackground(d);
    text::draw(d, title(), t.r(6), t.r(6.4f), {t.r(2.85f), Weight::Light, white(), Align::Left, VAlign::Middle});
    text::draw(d, prompt_, t.W() / 2, t.r(21.75f), {t.r(1.75f), Weight::Regular, bad_ ? hex(0xff9a8a) : white(.9f), Align::Center, VAlign::Middle});
    float avD = t.r(15.4f), ax = t.r(41.4f), ay = t.r(39.4f);
    if (user_.is_object()) widgets::avatar(d, ax, ay, avD, user_, t.r(5.6f));
    else {
        d.circle(ax, ay, avD / 2, hex(0x555555));
        text::draw(d, "?", ax, ay, {t.r(5.6f), Weight::Semibold, white(), Align::Center, VAlign::Middle});
    }
    TextStyle ns{t.r(1.6f), Weight::Regular, white(), Align::Center, VAlign::Middle};
    text::draw(d, name(), ax, ay + avD / 2 + t.r(1.4f) + text::lineHeight(ns.font()) / 2, ns);

    float bx = t.r(59.4f), by = t.r(30.75f), bw = t.r(27.1f);
    float sx = bx + t.r(shake());
    for (int i = 0; i < 4; i++) {
        Rect c{sx + i * t.r(6.9f), by, t.r(6.4f), t.r(5)};
        if (i == (int)digits_.size()) {
            d.round(c, t.r(.3f), hex(0x4c4e59));
            d.stroke(c, t.r(.3f), t.r(.12f), white());
        } else d.rect({c.x, c.y + c.h - t.r(.15f), c.w, t.r(.15f)}, white(.35f));
        if (i < (int)digits_.size()) d.circle(c.x + c.w / 2, c.y + c.h / 2, t.r(.5f), white());
    }
    Rect panel{bx, by + t.r(5) + t.r(1.4f), bw, t.r(1.2f * 2 + 5.4f * 4)};
    d.round(panel, t.r(.3f), rgba(32, 34, 44, .96f));
    float cw = (bw - t.r(1.2f)) / 3;
    TextStyle ds{t.r(1.9f), Weight::Regular, white(), Align::Left, VAlign::Middle};
    TextStyle es{t.r(1.25f), Weight::Bold, hex(0x1a1c24), Align::Center, VAlign::Middle};
    for (int i = 0; i < 12; i++) {
        if (!*KP[i]) continue;
        Rect c{panel.x + t.r(.6f) + (i % 3) * cw, panel.y + t.r(1.2f) + (i / 3) * t.r(5.4f), cw, t.r(5.4f)};
        if (i == sel_) {
            d.round(c, t.r(.4f), white(.1f));
            d.outline(c, t.r(.4f), t.r(.1f), white(.85f));
        }
        float dw = text::width(KP[i], ds.font());
        float ew = std::max(t.r(2.5f), text::width(padLabel(i), es.font()) + t.r(.6f));
        float x0 = c.x + (c.w - dw - t.r(.7f) - ew) / 2, cy = c.y + c.h / 2;
        text::draw(d, KP[i], x0, cy, ds);
        Rect e{x0 + dw + t.r(.7f), cy - t.r(1.05f), ew, t.r(2.1f)};
        d.round(e, t.r(.3f), white(.82f));
        text::draw(d, padLabel(i), e.x + e.w / 2, cy, es);
    }
    TextStyle note{t.r(1.3f), Weight::Regular, white(.6f), Align::Center, VAlign::Middle};
    glyph::line(d, glyph::has("back") ? "Type the digits, press the controller buttons shown, or click. {back} goes back."
                                      : "Type the digits, press the controller buttons shown, or click. Right-click goes back.",
                t.W() / 2, t.H() - t.r(3) - text::lineHeight(note.font()) / 2, note);
}

ANYPS5_SCREEN(Passcode, "passcode");

namespace pin {

void open(const std::string& mode, const json& user, const std::string& then, std::function<void()> changed) {
    Request& r = request();
    r.mode = mode;
    r.user = user;
    r.then = then;
    r.changed = std::move(changed);
    if (app().router.isOpen("passcode")) app().router.close("passcode");
    app().open("passcode");
}

}

}
