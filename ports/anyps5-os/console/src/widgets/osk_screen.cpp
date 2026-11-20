#include "core/App.h"
#include "widgets/osk.h"

namespace ui {

namespace {

struct Pending {
    std::string title, text;
    std::function<void(const std::string&)> done;
    osk::Opts opts;
};

Pending& pending() {
    static Pending p;
    return p;
}

}

class OskScreen : public Screen {
public:
    bool overlay() const override { return true; }
    bool typing() const override { return true; }

    void enter() override {
        Pending& p = pending();
        title_ = p.title;
        opts_ = p.opts;
        done_ = std::move(p.done);
        p.done = nullptr;
        field_.set(p.text);
        kb_.reset();
        sel_ = 0;
    }

    bool action(const Action& a) override {
        const std::string& k = a.name;
        if (k == "back") {
            done_ = nullptr;
            app().router.close(id);
        } else if (k == "square") press("\xE2\x8C\xAB");
        else if (k == "text") type(a.text);
        else if (k == "l1") press("Left");
        else if (k == "r1") press("Right");
        else if (k == "space" || k == "triangle") press("Space");
        else if (k == "enter" || k == "r2") press("Done");
        else if (k == "l2") press("Shift");
        else if (k == "ok") press(kb_.keyAt(sel_));
        else if (k == "left" || k == "right" || k == "up" || k == "down") sel_ = kb_.move(sel_, k);
        return true;
    }

    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, rgba(3, 6, 16, .86f));
        TextStyle ts{t.r(1.6f), Weight::Light, white(), Align::Center, VAlign::Middle};
        float titleH = text::lineHeight(ts.font());
        float total = titleH + t.r(1) + t.r(3.4f) + t.r(1.4f) + kb_.height();
        float y = (t.H() - total) / 2;
        text::draw(d, title_, t.W() / 2, y + titleH / 2, ts);
        y += titleH + t.r(1);
        float fw = t.r(40);
        Rect f{(t.W() - fw) / 2, y, fw, t.r(3.4f)};
        d.round(f, t.r(.6f), white(.1f));
        std::string shown = field_.str();
        if (opts_.password) {
            shown.clear();
            for (size_t i = 0; i < field_.text.size(); i++) shown += "\xE2\x80\xA2";
        }
        TextStyle fs{t.r(1.4f), Weight::Regular, white(), Align::Left, VAlign::Middle};
        d.pushClip(f);
        osk::caretText(d, shown, field_.cur, f.x + t.r(1.2f), f.y + f.h / 2, fs, t.r(1.4f) * 1.1f);
        d.popClip();
        y += t.r(3.4f) + t.r(1.4f);
        kb_.draw(d, (t.W() - t.r(54.4f)) / 2, y, sel_, true);
    }

private:
    void press(const std::string& k) {
        if (k.empty()) return;
        if (k == "Done") {
            auto done = std::move(done_);
            done_ = nullptr;
            std::string text = field_.str();
            app().router.close(id);
            if (done) done(text);
            return;
        }
        kb_.edit(field_, k, opts_.max, opts_.capFirst);
    }

    void type(const std::string& s) {
        for (char32_t c : osk::decode(s)) {
            if ((int)field_.text.size() >= opts_.max) break;
            std::u32string ch{c};
            if (field_.text.empty() && opts_.capFirst) ch = osk::decode(osk::upper(osk::encode(c)));
            field_.text.insert((size_t)field_.cur, ch);
            field_.cur += (int)ch.size();
        }
    }

    std::string title_;
    osk::Opts opts_;
    std::function<void(const std::string&)> done_;
    osk::Field field_;
    osk::Keyboard kb_;
    int sel_ = 0;
};

ANYPS5_SCREEN(OskScreen, "osk");

namespace osk {

void open(const std::string& title, const std::string& text, std::function<void(const std::string&)> done, Opts o) {
    Pending& p = pending();
    p.title = title;
    p.text = text;
    p.done = std::move(done);
    p.opts = o;
    if (app().router.isOpen("osk")) app().router.close("osk");
    app().open("osk");
}

}

}
