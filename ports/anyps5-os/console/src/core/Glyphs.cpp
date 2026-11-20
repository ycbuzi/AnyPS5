#include "core/Glyphs.h"
#include "core/App.h"
#include "core/Images.h"
#include <algorithm>
#include <vector>

namespace ui::glyph {

namespace {

enum class Kind { Icon, Ring, Key, KeyIcon, Pill, Word, Join };

struct Part {
    Kind kind;
    std::string s;
    Color tint = white();
    bool tinted = false;
};

enum class Face { South, East, West, North };

Source buttons(Source s) { return isPointer(s) ? app().input.buttonSource : s; }

std::vector<Part> one(Part p) {
    std::vector<Part> v;
    v.push_back(std::move(p));
    return v;
}

std::vector<Part> three(Part a, Part b, Part c) {
    std::vector<Part> v = one(std::move(a));
    v.push_back(std::move(b));
    v.push_back(std::move(c));
    return v;
}

Part face(Source s, Face f) {
    int i = (int)f;
    if (s == Source::PlayStation) {
        static const char* icons[] = {"gl-cross", "gl-circle", "gl-square", "gl-triangle"};
        return {Kind::Icon, icons[i]};
    }
    if (s == Source::Xbox) {
        static const char* letters[] = {"A", "B", "X", "Y"};
        static const Color tints[] = {hex(0x7ad66f), hex(0xf0705f), hex(0x62acf5), hex(0xf3cf55)};
        return {Kind::Ring, letters[i], tints[i], true};
    }
    if (s == Source::Nintendo) {
        static const char* letters[] = {"B", "A", "Y", "X"};
        return {Kind::Ring, letters[i]};
    }
    if (s == Source::Quest) {
        static const char* letters[] = {"A", "B", "X", "Y"};
        return {Kind::Ring, letters[i]};
    }
    static const char* icons[] = {"gl-south", "gl-east", "gl-west", "gl-north"};
    return {Kind::Icon, icons[i]};
}

const char* pick(Source s, const char* ps, const char* xbox, const char* nintendo, const char* generic) {
    return s == Source::PlayStation ? ps : s == Source::Xbox || s == Source::Quest ? xbox : s == Source::Nintendo ? nintendo : generic;
}

std::vector<Part> questParts(const std::string& a) {
    if (a == "create" || a == "touchpad" || a == "help") return {};
    if (a == "options") return one({Kind::Icon, "gl-menu"});
    if (a == "ps") return one({Kind::Pill, "Hold Menu"});
    if (a == "l1") return one({Kind::Pill, "L Grip"});
    if (a == "r1") return one({Kind::Pill, "R Grip"});
    if (a == "l2") return one({Kind::Pill, "L Trigger"});
    if (a == "r2" || a == "enter") return one({Kind::Pill, "R Trigger"});
    if (a == "l3") return one({Kind::Pill, "L Stick"});
    if (a == "r3") return one({Kind::Pill, "R Stick"});
    if (a == "zoom") return three({Kind::Pill, "L Stick"}, {Kind::Join, "+"}, {Kind::Pill, "R Stick"});
    if (a == "move") return one({Kind::Pill, "Left Stick"});
    return {};
}

std::vector<Part> padParts(const std::string& a, Source s) {
    bool south = app().input.okSouth(s);
    if (s == Source::Quest && a != "ok" && a != "back" && a != "square" && a != "triangle" && a != "space") return questParts(a);
    if (a == "ok") return one(face(s, south ? Face::South : Face::East));
    if (a == "back") return one(face(s, south ? Face::East : Face::South));
    if (a == "square") return one(face(s, Face::West));
    if (a == "triangle" || a == "space") return one(face(s, Face::North));
    if (a == "options") {
        if (s == Source::Generic) return one({Kind::Pill, "Start"});
        return one({Kind::Icon, s == Source::Nintendo ? "gl-plus" : "gl-menu"});
    }
    if (a == "create") {
        if (s == Source::Generic) return one({Kind::Pill, "Select"});
        return one({Kind::Icon, pick(s, "gl-create", "gl-view", "gl-minus", "")});
    }
    if (a == "ps") {
        if (s == Source::PlayStation) return one({Kind::Ring, "PS"});
        if (s == Source::Xbox) return one({Kind::Icon, "gl-xbox"});
        if (s == Source::Nintendo) return one({Kind::Icon, "gl-home"});
        if (app().input.padGuide()) return one({Kind::Pill, "Home"});
        return three({Kind::Pill, "Start"}, {Kind::Join, "+"}, {Kind::Pill, "Select"});
    }
    if (a == "l1") return one({Kind::Pill, pick(s, "L1", "LB", "L", "L1")});
    if (a == "r1") return one({Kind::Pill, pick(s, "R1", "RB", "R", "R1")});
    if (a == "l2") return one({Kind::Pill, pick(s, "L2", "LT", "ZL", "L2")});
    if (a == "r2" || a == "enter") return one({Kind::Pill, pick(s, "R2", "RT", "ZR", "R2")});
    if (a == "l3") return one({Kind::Pill, pick(s, "L3", "LS", "LS", "L3")});
    if (a == "r3") return one({Kind::Pill, pick(s, "R3", "RS", "RS", "R3")});
    if (a == "zoom") return three({Kind::Pill, pick(s, "L3", "LS", "LS", "L3")}, {Kind::Join, "+"}, {Kind::Pill, pick(s, "R3", "RS", "RS", "R3")});
    if (a == "touchpad") return s == Source::PlayStation ? one({Kind::Icon, "gl-touchpad"}) : std::vector<Part>{};
    if (a == "move") return one({Kind::Icon, "gl-dpad"});
    if (a == "overview") return one({Kind::KeyIcon, "gl-overview"});
    if (a == "help") return {};
    return one({Kind::Key, a});
}

std::vector<Part> keyParts(const std::string& a) {
    static const std::pair<const char*, const char*> keys[] = {{"ok", "Enter"}, {"enter", "Enter"}, {"r2", "Enter"}, {"back", "Esc"},
        {"options", "O"}, {"help", "H"}, {"r1", "Tab"}, {"l1", "PgUp"}, {"create", "C"}, {"square", "Del"}, {"triangle", "/"},
        {"space", "Space"}, {"l2", "Shift"}, {"zoom", "Z"}};
    if (a == "ps") {
        if (app().input.overview) return one({Kind::KeyIcon, "gl-overview"});
        return one({Kind::Key, "Q"});
    }
    if (a == "overview") return one({Kind::KeyIcon, "gl-overview"});
    if (a == "move") return one({Kind::KeyIcon, "gl-arrows"});
    if (a == "l3" || a == "r3" || a == "touchpad") return {};
    for (auto& k : keys)
        if (a == k.first) return one({Kind::Key, k.second});
    return one({Kind::Key, a});
}

std::vector<Part> parts(const std::string& a, Source s) {
    if (isPointer(s) && (a == "ok" || a == "enter")) return one({Kind::Word, s == Source::Touch ? "Tap" : "Click"});
    Source b = buttons(s);
    return isPad(b) ? padParts(a, b) : keyParts(a);
}

Font partFont(const Part& p, float h) {
    switch (p.kind) {
    case Kind::Ring: return {h * (p.s.size() > 1 ? .42f : .56f), Weight::Semibold};
    case Kind::Key: return {h * .525f, Weight::Semibold};
    case Kind::Pill: return {h * .5f, Weight::Semibold};
    case Kind::Word: return {h * .625f, Weight::Semibold};
    default: return {h * .5f, Weight::Regular};
    }
}

float partWidth(const Part& p, float h) {
    switch (p.kind) {
    case Kind::Icon:
    case Kind::Ring: return h;
    case Kind::Key: return std::max(h, text::width(p.s, partFont(p, h)) + h * .45f);
    case Kind::KeyIcon: return h * 1.3f;
    case Kind::Pill: return std::max(h * 1.3f, text::width(p.s, partFont(p, h)) + h * .7f);
    case Kind::Word: return text::width(p.s, partFont(p, h));
    case Kind::Join: return text::width(p.s, partFont(p, h)) + h * .2f;
    }
    return h;
}

float gapOf(float h) { return h * .12f; }

float partsWidth(const std::vector<Part>& ps, float h) {
    float w = 0;
    for (size_t i = 0; i < ps.size(); i++) w += partWidth(ps[i], h) + (i ? gapOf(h) : 0);
    return w;
}

void drawPart(Draw& d, const Part& p, float x, float cy, float h, Color c) {
    float w = partWidth(p, h);
    Rect box{x, cy - h / 2, w, h};
    float lw = std::max(1.f, h * .05f);
    Font f = partFont(p, h);
    bool contrast = app().theme.highContrast;
    switch (p.kind) {
    case Kind::Icon:
        d.icon(images::icon(p.s, h), x + h / 2, cy, h, c);
        return;
    case Kind::Ring:
        d.stroke(box, h / 2, std::max(1.f, h * .06f), alpha(c, contrast ? .9f : .5f));
        text::draw(d, p.s, x + w / 2, cy, {f.size, f.weight, p.tinted && !contrast ? alpha(p.tint, c.a) : c, Align::Center, VAlign::Middle});
        return;
    case Kind::Key:
        d.stroke(box, h * .225f, lw, alpha(c, contrast ? .9f : .38f));
        text::draw(d, p.s, x + w / 2, cy, {f.size, f.weight, c, Align::Center, VAlign::Middle});
        return;
    case Kind::KeyIcon:
        d.stroke(box, h * .225f, lw, alpha(c, contrast ? .9f : .38f));
        d.icon(images::icon(p.s, h * .72f), x + w / 2, cy, h * .72f, c);
        return;
    case Kind::Pill: {
        Rect r{x, cy - h * .42f, w, h * .84f};
        d.stroke(r, r.h / 2, lw, alpha(c, contrast ? .9f : .5f));
        text::draw(d, p.s, x + w / 2, cy, {f.size, f.weight, c, Align::Center, VAlign::Middle});
        return;
    }
    case Kind::Word:
        text::draw(d, p.s, x, cy, {f.size, f.weight, c, Align::Left, VAlign::Middle});
        return;
    case Kind::Join:
        text::draw(d, p.s, x + w / 2, cy, {f.size, f.weight, alpha(c, .62f), Align::Center, VAlign::Middle});
        return;
    }
}

struct Seg {
    bool token;
    std::string s;
};

std::vector<Seg> split(const std::string& t) {
    std::vector<Seg> out;
    size_t i = 0;
    while (i < t.size()) {
        size_t a = t.find('{', i);
        size_t b = a == std::string::npos ? a : t.find('}', a);
        if (b == std::string::npos) {
            out.push_back({false, t.substr(i)});
            break;
        }
        if (a > i) out.push_back({false, t.substr(i, a - i)});
        out.push_back({true, t.substr(a + 1, b - a - 1)});
        i = b + 1;
    }
    return out;
}

float glyphH(float size) { return size * 1.45f; }

const char* padName(const std::string& a, Source s) {
    bool south = app().input.okSouth(s);
    Face f = Face::South;
    bool isFace = true;
    if (a == "ok") f = south ? Face::South : Face::East;
    else if (a == "back") f = south ? Face::East : Face::South;
    else if (a == "square") f = Face::West;
    else if (a == "triangle" || a == "space") f = Face::North;
    else isFace = false;
    if (isFace) {
        static const char* ps[] = {"Cross", "Circle", "Square", "Triangle"};
        static const char* xb[] = {"A", "B", "X", "Y"};
        static const char* nt[] = {"B", "A", "Y", "X"};
        static const char* gn[] = {"the bottom button", "the right button", "the left button", "the top button"};
        return (s == Source::PlayStation ? ps : s == Source::Xbox || s == Source::Quest ? xb : s == Source::Nintendo ? nt : gn)[(int)f];
    }
    if (s == Source::Quest) {
        if (a == "options") return "the Menu button";
        if (a == "ps") return "the Menu button held down";
        if (a == "l1") return "the left grip";
        if (a == "r1") return "the right grip";
        if (a == "l2") return "the left trigger";
        if (a == "r2" || a == "enter") return "the right trigger";
        if (a == "l3") return "the left stick button";
        if (a == "r3") return "the right stick button";
        if (a == "zoom") return "both stick buttons together";
        if (a == "move") return "the left stick";
        return nullptr;
    }
    if (a == "options") return pick(s, "Options", "Menu", "Plus", "Start");
    if (a == "create") return pick(s, "Create", "View", "Minus", "Select");
    if (a == "ps") return pick(s, "the PS button", "the Xbox button", "the Home button", app().input.padGuide() ? "the Home button" : "Start + Select");
    if (a == "l1") return pick(s, "L1", "LB", "L", "L1");
    if (a == "r1") return pick(s, "R1", "RB", "R", "R1");
    if (a == "l2") return pick(s, "L2", "LT", "ZL", "L2");
    if (a == "r2" || a == "enter") return pick(s, "R2", "RT", "ZR", "R2");
    if (a == "l3") return pick(s, "L3", "the left stick button", "the left stick button", "L3");
    if (a == "r3") return pick(s, "R3", "the right stick button", "the right stick button", "R3");
    if (a == "zoom") return pick(s, "L3 and R3 together", "both stick buttons together", "both stick buttons together", "L3 and R3 together");
    if (a == "touchpad") return "the touchpad";
    if (a == "move") return "the D-pad";
    return nullptr;
}

}

Source current() { return app().input.source; }
Source pad() { return app().input.padSource(); }

bool has(const std::string& action, Source s) { return !isPointer(s) && !parts(action, s).empty(); }

float width(const std::string& action, float h, Source s) { return partsWidth(parts(action, s), h); }

float draw(Draw& d, const std::string& action, float x, float cy, float h, Color c, Source s) {
    auto ps = parts(action, s);
    float at = x;
    for (size_t i = 0; i < ps.size(); i++) {
        if (i) at += gapOf(h);
        drawPart(d, ps[i], at, cy, h, c);
        at += partWidth(ps[i], h);
    }
    return at - x;
}

std::string name(const std::string& action, Source s) {
    if (isPointer(s)) {
        bool touch = s == Source::Touch;
        if (action == "ok" || action == "enter") return touch ? "tap" : "click";
        if (action == "back" && !touch) return "right-click";
        if (action == "options" && !touch) return "middle-click";
    }
    Source b = buttons(s);
    if (isPad(b)) {
        const char* n = padName(action, b);
        return n ? n : action;
    }
    if (action == "ps") return app().input.overview ? "the Overview key" : "Q";
    if (action == "overview") return "the Overview key";
    if (action == "move") return "the arrow keys";
    auto ps = keyParts(action);
    return ps.empty() ? action : ps[0].s;
}

std::string fill(const std::string& templ, Source s) {
    std::string out;
    for (auto& g : split(templ)) out += g.token ? name(g.s, s) : g.s;
    return out;
}

std::string swapLabel(Source s) {
    Source b = isPad(s) ? s : Source::PlayStation;
    return b == Source::PlayStation ? "Swap Cross and Circle" : b == Source::Generic ? "Swap the Bottom and Right Buttons" : "Swap A and B";
}

std::string swapNote(Source s) {
    Source b = isPad(s) ? s : Source::PlayStation;
    const char* ok = b == Source::PlayStation ? "Cross" : b == Source::Generic ? "the bottom button" : "A";
    const char* back = b == Source::PlayStation ? "Circle" : b == Source::Generic ? "the right button" : "B";
    return std::string("It makes ") + back + " select and " + ok + " go back on the AnyPS5 screen; games keep their own buttons.";
}

float lineWidth(const std::string& templ, Font f, Source s) {
    float w = 0, h = glyphH(f.size), pad = f.size * .28f;
    auto segs = split(templ);
    for (size_t i = 0; i < segs.size(); i++) {
        if (!segs[i].token) {
            w += text::width(segs[i].s, f);
            continue;
        }
        float gw = width(segs[i].s, h, s);
        if (gw <= 0) continue;
        bool glueL = i > 0 && !segs[i - 1].s.empty() && segs[i - 1].s.back() != ' ';
        bool glueR = i + 1 < segs.size() && !segs[i + 1].s.empty() && segs[i + 1].s.front() != ' ';
        w += gw + (glueL ? pad : 0) + (glueR ? pad : 0);
    }
    return w;
}

float line(Draw& d, const std::string& templ, float x, float y, const TextStyle& st, Source s) {
    Font f = st.font();
    float total = lineWidth(templ, f, s), h = glyphH(f.size), pad = f.size * .28f;
    float at = st.align == Align::Center ? x - total / 2 : st.align == Align::Right ? x - total : x;
    float cy = st.valign == VAlign::Middle ? y : st.valign == VAlign::Top ? y + text::lineHeight(f) / 2 : y - text::ascent(f) * .34f;
    TextStyle ts = st;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    auto segs = split(templ);
    for (size_t i = 0; i < segs.size(); i++) {
        if (!segs[i].token) {
            at += text::draw(d, segs[i].s, at, cy, ts);
            continue;
        }
        float gw = width(segs[i].s, h, s);
        if (gw <= 0) continue;
        bool glueL = i > 0 && !segs[i - 1].s.empty() && segs[i - 1].s.back() != ' ';
        bool glueR = i + 1 < segs.size() && !segs[i + 1].s.empty() && segs[i + 1].s.front() != ' ';
        if (glueL) at += pad;
        draw(d, segs[i].s, at, cy, h, st.color, s);
        at += gw + (glueR ? pad : 0);
    }
    return total;
}

}
