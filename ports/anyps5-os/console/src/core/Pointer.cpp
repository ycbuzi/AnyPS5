#include "core/Pointer.h"
#include "core/Anim.h"
#include "core/App.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ui::pointer {

namespace {

struct Target {
    Rect r;
    float radius;
    std::function<void()> hover, click;
};

struct Area {
    Rect r;
    std::function<void(float)> scroll;
};

struct Finger {
    SDL_FingerID id;
    float x0, y0, x, y;
};

struct Click {
    float x, y;
};

struct State {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    bool drawn = false;
    bool hw = false;
    float x = -1, y = -1;
    bool shown = false;
    bool active = false;
    double lastMove = -100;
    bool hittable = true;
    Draw* draw = nullptr;
    std::vector<Target> targets;
    std::vector<Area> areas;
    bool moved = false;
    std::vector<Click> clicks;
    float wheel = 0, wheelX = 0, wheelY = 0, wheelAcc = 0;
    int hovered = -1;
    Rect hoverRect{};
    Screen* hoverScreen = nullptr;
    std::vector<Finger> touch, pad;
    double touchT0 = 0, padT0 = 0;
    bool touchMoved = false, padMoved = false;
    size_t padMax = 0;
    bool sysShown = false;
};

State& s() {
    static State st;
    return st;
}

float rowPx() { return app().theme.r(4.5f); }

void showNow() {
    State& st = s();
    st.shown = true;
    st.active = true;
    st.lastMove = now();
}

void toRender(float wx, float wy, float& rx, float& ry) {
    State& st = s();
    rx = wx;
    ry = wy;
    if (st.ren) SDL_RenderCoordinatesFromWindow(st.ren, wx, wy, &rx, &ry);
}

void clampPos() {
    State& st = s();
    const Theme& t = app().theme;
    st.x = std::clamp(st.x, 0.f, std::max(0.f, t.W() - 1));
    st.y = std::clamp(st.y, 0.f, std::max(0.f, t.H() - 1));
}

void addWheel(float rows, float px, float py) {
    State& st = s();
    st.wheel += rows;
    st.wheelX = px;
    st.wheelY = py;
}

bool inside(const Target& t, float px, float py) {
    if (!contains(t.r, px, py)) return false;
    float rad = std::min(t.radius, std::min(t.r.w, t.r.h) / 2);
    if (rad <= 0) return true;
    float cx = std::clamp(px, t.r.x + rad, t.r.x + t.r.w - rad);
    float cy = std::clamp(py, t.r.y + rad, t.r.y + t.r.h - rad);
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= rad * rad;
}

bool sameRect(const Rect& a, const Rect& b) {
    return std::fabs(a.x - b.x) < .5f && std::fabs(a.y - b.y) < .5f && std::fabs(a.w - b.w) < .5f && std::fabs(a.h - b.h) < .5f;
}

bool fresh(int h) {
    State& st = s();
    return h != st.hovered || (h >= 0 && !sameRect(st.targets[(size_t)h].r, st.hoverRect));
}

int hit(float px, float py) {
    State& st = s();
    for (size_t i = st.targets.size(); i-- > 0;)
        if (inside(st.targets[i], px, py)) return (int)i;
    return -1;
}

Finger* findFinger(std::vector<Finger>& v, SDL_FingerID id) {
    for (auto& f : v)
        if (f.id == id) return &f;
    return nullptr;
}

void eraseFinger(std::vector<Finger>& v, SDL_FingerID id) {
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].id == id) {
            v.erase(v.begin() + (long)i);
            return;
        }
}

void touchscreen(const SDL_Event& e) {
    State& st = s();
    const Theme& t = app().theme;
    float px = e.tfinger.x * t.W(), py = e.tfinger.y * t.H();
    float slop = t.r(1.2f);
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (st.touch.empty()) {
            st.touchT0 = now();
            st.touchMoved = false;
        }
        st.touch.push_back({e.tfinger.fingerID, px, py, px, py});
        st.active = true;
        st.shown = false;
        return;
    }
    Finger* f = findFinger(st.touch, e.tfinger.fingerID);
    if (!f) return;
    if (e.type == SDL_EVENT_FINGER_MOTION) {
        float dy = py - f->y;
        f->x = px;
        f->y = py;
        if (std::fabs(px - f->x0) > slop || std::fabs(py - f->y0) > slop) st.touchMoved = true;
        if (st.touchMoved && &st.touch.front() == f) addWheel(-dy / rowPx(), f->x0, f->y0);
        return;
    }
    bool first = &st.touch.front() == f;
    float x0 = f->x0, y0 = f->y0;
    eraseFinger(st.touch, e.tfinger.fingerID);
    if (first && st.touch.empty() && !st.touchMoved && now() - st.touchT0 < .6) {
        st.x = x0;
        st.y = y0;
        st.moved = true;
        st.clicks.push_back({x0, y0});
    }
}

void touchpad(const SDL_Event& e, std::vector<Action>& out) {
    State& st = s();
    const Theme& t = app().theme;
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (st.pad.empty()) {
            st.padT0 = now();
            st.padMoved = false;
            st.padMax = 0;
        }
        st.pad.push_back({e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, e.tfinger.x, e.tfinger.y});
        st.padMax = std::max(st.padMax, st.pad.size());
        return;
    }
    Finger* f = findFinger(st.pad, e.tfinger.fingerID);
    if (!f) return;
    if (e.type == SDL_EVENT_FINGER_MOTION) {
        float dx = e.tfinger.x - f->x, dy = e.tfinger.y - f->y;
        f->x = e.tfinger.x;
        f->y = e.tfinger.y;
        if (std::fabs(f->x - f->x0) > .02f || std::fabs(f->y - f->y0) > .02f) st.padMoved = true;
        if (st.pad.size() == 1) {
            float speed = 1.4f;
            st.x += dx * t.W() * speed;
            st.y += dy * t.H() * speed;
            clampPos();
            if (st.win && st.ren) {
                float wx = st.x, wy = st.y;
                SDL_RenderCoordinatesToWindow(st.ren, st.x, st.y, &wx, &wy);
                SDL_WarpMouseInWindow(st.win, wx, wy);
            }
            st.moved = true;
            showNow();
        } else if (st.pad.size() == 2) {
            addWheel(-dy * t.H() / rowPx() / 2, st.x, st.y);
            showNow();
        }
        return;
    }
    eraseFinger(st.pad, e.tfinger.fingerID);
    if (!st.pad.empty() || st.padMoved || now() - st.padT0 > .25) return;
    showNow();
    if (st.padMax == 1) st.clicks.push_back({st.x, st.y});
    else if (st.padMax == 2) out.push_back({"back", ""});
}

}

void target(const Rect& r, std::function<void()> onHover, std::function<void()> onClick, float radius) {
    State& st = s();
    if (!st.hittable) return;
    Rect c = r;
    if (st.draw) {
        if (st.draw->alphaMul() <= .02f) return;
        if (!st.draw->clipTo(c)) return;
    } else if (c.w <= 0 || c.h <= 0) {
        return;
    }
    st.targets.push_back({c, radius, std::move(onHover), std::move(onClick)});
}

void scrollArea(const Rect& r, std::function<void(float)> onScroll) {
    State& st = s();
    if (!st.hittable) return;
    Rect c = r;
    if (st.draw && !st.draw->clipTo(c)) return;
    st.areas.push_back({c, std::move(onScroll)});
}

bool active() { return s().active; }
bool visible() { return s().shown; }
float x() { return s().x; }
float y() { return s().y; }

void init(SDL_Window* w, SDL_Renderer* r, bool drawnCursor) {
    State& st = s();
    st.win = w;
    st.ren = r;
    const char* hw = std::getenv("ANYPS5_HW_CURSOR");
    st.hw = hw && *hw == '1';
    st.drawn = drawnCursor && !st.hw;
    st.x = app().theme.W() / 2;
    st.y = app().theme.H() / 2;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (w && !app().deterministic) {
        int n = 0;
        SDL_TouchID* ids = SDL_GetTouchDevices(&n);
        for (int i = 0; i < n; i++) {
            const char* name = SDL_GetTouchDeviceName(ids[i]);
            SDL_TouchDeviceType ty = SDL_GetTouchDeviceType(ids[i]);
            SDL_Log("pointer: touch device '%s' %s", name ? name : "?", ty == SDL_TOUCH_DEVICE_DIRECT ? "touchscreen" : ty == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE ? "touchpad (absolute)" : ty == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE ? "touchpad (relative)" : "unknown");
        }
        SDL_free(ids);
        int m = 0;
        SDL_MouseID* mice = SDL_GetMice(&m);
        for (int i = 0; i < m; i++) {
            const char* name = SDL_GetMouseNameForID(mice[i]);
            SDL_Log("pointer: mouse '%s'", name ? name : "?");
        }
        SDL_free(mice);
        SDL_Log("pointer: %d touch device(s), %d mouse device(s), cursor %s", n, m, st.drawn ? "drawn" : "system");
    }
}

void handle(const SDL_Event& e, std::vector<Action>& out) {
    State& st = s();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        if (e.motion.which == SDL_TOUCH_MOUSEID) return;
        toRender(e.motion.x, e.motion.y, st.x, st.y);
        st.moved = true;
        showNow();
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (e.button.which == SDL_TOUCH_MOUSEID) return;
        showNow();
        if (e.button.button == SDL_BUTTON_LEFT) st.clicks.push_back({st.x, st.y});
        else if (e.button.button == SDL_BUTTON_RIGHT) out.push_back({"back", ""});
        else if (e.button.button == SDL_BUTTON_MIDDLE) out.push_back({"options", ""});
        else if (e.button.button == SDL_BUTTON_X1) out.push_back({"back", ""});
        return;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (e.wheel.which == SDL_TOUCH_MOUSEID) return;
        float dy = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f);
        addWheel(-dy, st.x, st.y);
        showNow();
        return;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (SDL_GetTouchDeviceType(e.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT) {
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                st.touch.clear();
                return;
            }
            touchscreen(e);
        } else {
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                st.pad.clear();
                return;
            }
            touchpad(e, out);
        }
        return;
    default: return;
    }
}

void keyUsed() {
    State& st = s();
    st.shown = false;
    st.active = false;
}

void beginFrame(Draw& d) {
    State& st = s();
    st.draw = &d;
    st.targets.clear();
    st.areas.clear();
    st.hittable = true;
}

void layer(bool hittable) { s().hittable = hittable; }

void endFrame(std::vector<Action>& out) {
    State& st = s();
    st.hittable = true;
    st.draw = nullptr;
    if (st.shown && now() - st.lastMove > 3.0) st.shown = false;
    Screen* top = app().router.top();
    if (top != st.hoverScreen) {
        st.hoverScreen = top;
        st.hovered = -1;
    }
    if (st.moved) {
        st.moved = false;
        int h = hit(st.x, st.y);
        if (fresh(h)) {
            st.hovered = h;
            if (h >= 0) st.hoverRect = st.targets[(size_t)h].r;
            if (h >= 0 && st.targets[(size_t)h].hover) {
                auto f = st.targets[(size_t)h].hover;
                f();
            }
        }
    }
    if (!st.clicks.empty()) {
        Click c = st.clicks.front();
        st.clicks.erase(st.clicks.begin());
        int h = hit(c.x, c.y);
        if (h >= 0) {
            Target tg = st.targets[(size_t)h];
            if (fresh(h)) {
                st.hovered = h;
                st.hoverRect = tg.r;
                if (tg.hover) tg.hover();
            }
            if (tg.click) tg.click();
        }
    } else if (st.wheel != 0) {
        float rows = st.wheel;
        st.wheel = 0;
        const Area* a = nullptr;
        for (size_t i = st.areas.size(); i-- > 0;)
            if (contains(st.areas[i].r, st.wheelX, st.wheelY)) {
                a = &st.areas[i];
                break;
            }
        if (a) {
            st.wheelAcc = 0;
            auto f = a->scroll;
            f(rows);
        } else {
            st.wheelAcc += rows;
            while (st.wheelAcc >= 1) {
                st.wheelAcc -= 1;
                out.push_back({"down", ""});
            }
            while (st.wheelAcc <= -1) {
                st.wheelAcc += 1;
                out.push_back({"up", ""});
            }
        }
    }
    if (!st.drawn && st.win) {
        bool want = st.shown || (st.hw && st.active);
        if (want != st.sysShown) {
            st.sysShown = want;
            if (want) SDL_ShowCursor();
            else SDL_HideCursor();
        }
    }
}

void drawCursor(Draw& d) {
    State& st = s();
    if (!st.drawn || !st.shown) return;
    const Theme& t = app().theme;
    float age = (float)(now() - st.lastMove);
    float a = age < 2.6f ? 1.f : std::max(0.f, 1.f - (age - 2.6f) / .4f);
    float r = t.r(.75f);
    d.pushAlpha(a);
    d.dot(st.x, st.y + t.r(.08f), r * 1.9f, black(.35f), black(0));
    d.circle(st.x, st.y, r + t.r(.14f), black(.55f));
    d.circle(st.x, st.y, r, white(.92f));
    d.circle(st.x, st.y, r * .28f, hex(0x2f6fe0));
    d.popAlpha();
}

void simMove(float px, float py) {
    State& st = s();
    st.x = px;
    st.y = py;
    clampPos();
    st.moved = true;
    showNow();
}

void simClick(float px, float py, int button) {
    simMove(px, py);
    if (button == 1) s().clicks.push_back({s().x, s().y});
}

void simWheel(float rows) {
    State& st = s();
    addWheel(rows, st.x, st.y);
    showNow();
}

}
