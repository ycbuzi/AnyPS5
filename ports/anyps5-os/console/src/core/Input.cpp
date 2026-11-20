#include "core/Input.h"
#include "core/Pointer.h"
#include "platform/Platform.h"
#include <algorithm>
#include <cmath>

namespace ui {

static constexpr double kHoldOffSeconds = 0.35;
static constexpr double kTravelWindowSeconds = 0.5;
static constexpr float kTravelPx = 16.f;
static constexpr double kChordGameSeconds = 0.5;
static constexpr float kStickSource = .55f;
static constexpr float kTriggerSource = .5f;

static const char* keyAction(SDL_Keycode k, bool typing) {
    switch (k) {
    case SDLK_LEFT: return "left";
    case SDLK_RIGHT: return "right";
    case SDLK_UP: return "up";
    case SDLK_DOWN: return "down";
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return typing ? "enter" : "ok";
    case SDLK_ESCAPE: return "back";
    case SDLK_BACKSPACE: return typing ? "square" : "back";
    case SDLK_DELETE: return "square";
    case SDLK_TAB: return "tab";
    case SDLK_PAGEUP: return "l1";
    case SDLK_PAGEDOWN: return "r1";
    case SDLK_F11: return "fullscreen";
    case SDLK_AC_BACK: return "back";
    case SDLK_SPACE: return typing ? nullptr : "ok";
    default: break;
    }
    if (typing) return nullptr;
    switch (k) {
    case SDLK_Z: return "zoom";
    case SDLK_C: return "create";
    case SDLK_Q: return "ps";
    case SDLK_H: return "help";
    case SDLK_O: return "options";
    case SDLK_A: return "left";
    case SDLK_D: return "right";
    case SDLK_W: return "up";
    case SDLK_S: return "down";
    case SDLK_SLASH: return "triangle";
    default: break;
    }
    return nullptr;
}

void Input::setHoldSpeed(const std::string& speed) {
    if (speed == "slow") { holdDelayMs = 600; repeatMs = 200; }
    else if (speed == "fast") { holdDelayMs = 250; repeatMs = 60; }
    else { holdDelayMs = 380; repeatMs = 110; }
}

void Input::setTyping(bool on, SDL_Window* w) {
    if (on == typing_) return;
    typing_ = on;
    if (on) SDL_StartTextInput(w);
    else SDL_StopTextInput(w);
}

void Input::setSource(Source s) {
    if (!isPointer(s)) buttonSource = s;
    if (s == source) return;
    source = s;
    lastPad = isPad(s);
    SDL_Log("input source: %s", sourceId(s));
}

void Input::firm(Source s) {
    firmAt_ = now_;
    travel_ = 0;
    setSource(s);
}

void Input::soft(float px) {
    if (source == Source::Mouse) return;
    if (now_ - firmAt_ < kHoldOffSeconds) return;
    if (now_ - travelAt_ > kTravelWindowSeconds) {
        travel_ = 0;
        travelAt_ = now_;
    }
    travel_ += px;
    if (travel_ < kTravelPx) return;
    travel_ = 0;
    setSource(Source::Mouse);
}

void Input::observePointer(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID || fromPadPointer(false, e.motion.which)) return;
        soft(std::fabs(e.motion.xrel) + std::fabs(e.motion.yrel));
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which == SDL_TOUCH_MOUSEID || fromPadPointer(false, e.button.which)) return;
        firm(Source::Mouse);
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.which == SDL_TOUCH_MOUSEID || fromPadPointer(false, e.wheel.which)) return;
        firm(Source::Mouse);
        return;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        bool direct = directTouch ? directTouch(e.tfinger.touchID) : SDL_GetTouchDeviceType(e.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT;
        if (direct) {
            if (e.type == SDL_EVENT_FINGER_DOWN) firm(Source::Touch);
            return;
        }
        if (fromPadPointer(true, e.tfinger.touchID)) return;
        if (e.type == SDL_EVENT_FINGER_MOTION) soft((std::fabs(e.tfinger.dx) + std::fabs(e.tfinger.dy)) * 1000.f);
        else soft(kTravelPx / 2);
        return;
    }
    default: return;
    }
}

void Input::padButton(const std::string& name, bool down, double now, std::vector<Action>& out) {
    int& h = holders_[name];
    bool was = h > 0;
    h = std::max(0, h + (down ? 1 : -1));
    bool is = h > 0;
    if (is && !was) {
        pointer::keyUsed();
        out.push_back({name, "", false, true});
        nextAt_[name] = now + holdDelayMs / 1000.0;
    }
    held_[name] = is;
}

bool Input::chordButton(Pad& p, bool start, bool down, std::vector<Action>& out) {
    bool& mine = start ? p.start : p.back;
    bool other = start ? p.back : p.start;
    if (down) {
        mine = true;
        if (other && !p.chord) {
            p.chord = true;
            p.chordSent = false;
            p.chordUp = false;
            p.chordAt = now_;
            pointer::keyUsed();
            if (!inGame) out.push_back({"ps", "", false, true});
        }
        return true;
    }
    bool was = mine;
    mine = false;
    if (p.chord) {
        if (p.chordSent && !p.chordUp) {
            p.chordUp = true;
            chordEdges_.push_back(-1);
        }
        if (!other) p.chord = false;
        return true;
    }
    if (was) {
        pointer::keyUsed();
        out.push_back({start ? "options" : "create", "", false, true});
    }
    return true;
}

int Input::takeChordEdge() {
    if (chordEdges_.empty()) return 0;
    int e = chordEdges_.front();
    chordEdges_.erase(chordEdges_.begin());
    return e;
}

void Input::handle(const SDL_Event& e, std::vector<Action>& out) {
    observePointer(e);
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return;
        firm(Source::Keyboard);
        pointer::keyUsed();
        SDL_Keycode k = e.key.key;
        if (e.key.scancode == SDL_SCANCODE_UNKNOWN && platform::isPsKey((int)e.key.raw)) {
            if (!typing_) out.push_back({"ps", "", e.key.repeat, false});
            return;
        }
        if (!typing_ && k >= SDLK_0 && k <= SDLK_9) {
            out.push_back({std::string(1, (char)('0' + (k - SDLK_0))), "", e.key.repeat, false});
            return;
        }
        if (k == SDLK_SLASH && (e.key.mod & SDL_KMOD_SHIFT) && !typing_) {
            out.push_back({"help", "", e.key.repeat, false});
            return;
        }
        if (const char* a = keyAction(k, typing_)) out.push_back({a, "", e.key.repeat, false});
        return;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (typing_ && e.text.text) out.push_back({std::string(e.text.text) == " " ? "space" : "text", e.text.text, false, false});
        return;
    case SDL_EVENT_JOYSTICK_ADDED:
        joystickAdded(e.jdevice.which);
        return;
    case SDL_EVENT_JOYSTICK_REMOVED:
        fallback_.erase(e.jdevice.which);
        return;
    case SDL_EVENT_GAMEPAD_ADDED:
        openPad(e.gdevice.which);
        return;
    case SDL_EVENT_GAMEPAD_REMOVED: {
        auto it = pads_.find(e.gdevice.which);
        if (it != pads_.end()) {
            SDL_Log("pad: '%s' removed", it->second.info.name.c_str());
            if (it->second.g) SDL_CloseGamepad(it->second.g);
            pads_.erase(it);
            padPointer_.clear();
        }
        return;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        auto it = pads_.find(e.gbutton.which);
        Source family = it != pads_.end() ? it->second.info.family : Source::Generic;
        if (down) {
            firm(family);
            lastPadId_ = e.gbutton.which;
        }
        if (it != pads_.end() && !it->second.info.guide &&
            (e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            chordButton(it->second, e.gbutton.button == SDL_GAMEPAD_BUTTON_START, down, out);
            return;
        }
        bool south = okSouth(family);
        const char* n = nullptr;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: n = south ? "ok" : "back"; break;
        case SDL_GAMEPAD_BUTTON_EAST: n = south ? "back" : "ok"; break;
        case SDL_GAMEPAD_BUTTON_WEST: n = "square"; break;
        case SDL_GAMEPAD_BUTTON_NORTH: n = "triangle"; break;
        case SDL_GAMEPAD_BUTTON_BACK: n = "create"; break;
        case SDL_GAMEPAD_BUTTON_START: n = "options"; break;
        case SDL_GAMEPAD_BUTTON_GUIDE: n = "ps"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: n = "l1"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: n = "r1"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: n = "l3"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: n = "r3"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: n = "up"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: n = "down"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: n = "left"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: n = "right"; break;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD: n = "touchpad"; break;
        default: break;
        }
        if (!n) return;
        padButton(n, down, now_, out);
        if (down && held_["l3"] && held_["r3"] && (std::string(n) == "l3" || std::string(n) == "r3")) out.push_back({"zoom", "", false, true});
        return;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.f;
        bool trigger = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        if (trigger ? v > kTriggerSource : std::fabs(v) > kStickSource) {
            auto it = pads_.find(e.gaxis.which);
            firm(it != pads_.end() ? it->second.info.family : Source::Generic);
            lastPadId_ = e.gaxis.which;
        }
        auto edge = [&](const std::string& n, bool on) {
            std::string key = "axis:" + n;
            if (held_[key] == on) return;
            held_[key] = on;
            padButton(n, on, now_, out);
        };
        switch (e.gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: edge("left", v < -.55f); edge("right", v > .55f); break;
        case SDL_GAMEPAD_AXIS_LEFTY: edge("up", v < -.55f); edge("down", v > .55f); break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: edge("l2", v > .5f); break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: edge("r2", v > .5f); break;
        default: break;
        }
        return;
    }
    default: return;
    }
}

void Input::update(double now, std::vector<Action>& out) {
    now_ = now;
    for (auto& kv : pads_) {
        Pad& p = kv.second;
        if (p.chord && inGame && !p.chordSent && now - p.chordAt >= kChordGameSeconds) {
            p.chordSent = true;
            chordEdges_.push_back(1);
        }
    }
    for (const char* d : {"left", "right", "up", "down"}) {
        if (!held_[d]) continue;
        if (now >= nextAt_[d]) {
            nextAt_[d] = now + repeatMs / 1000.0;
            out.push_back({d, "", true, true});
        }
    }
}

}
