#include "core/Input.h"
#include <cstdio>

namespace ui {

namespace {

int gFails = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) gFails++;
}

SDL_Event blank(Uint32 type) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    return e;
}

SDL_Event key(SDL_Keycode k) {
    SDL_Event e = blank(SDL_EVENT_KEY_DOWN);
    e.key.key = k;
    e.key.scancode = SDL_SCANCODE_A;
    return e;
}

SDL_Event button(SDL_JoystickID id, int b, bool down) {
    SDL_Event e = blank(down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP);
    e.gbutton.which = id;
    e.gbutton.button = (Uint8)b;
    e.gbutton.down = down;
    return e;
}

SDL_Event axis(SDL_JoystickID id, int a, float v) {
    SDL_Event e = blank(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    e.gaxis.which = id;
    e.gaxis.axis = (Uint8)a;
    e.gaxis.value = (Sint16)(v * 32767.f);
    return e;
}

SDL_Event motion(float dx, float dy) {
    SDL_Event e = blank(SDL_EVENT_MOUSE_MOTION);
    e.motion.which = 1;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    return e;
}

SDL_Event click() {
    SDL_Event e = blank(SDL_EVENT_MOUSE_BUTTON_DOWN);
    e.button.which = 1;
    e.button.button = SDL_BUTTON_LEFT;
    return e;
}

SDL_Event wheel() {
    SDL_Event e = blank(SDL_EVENT_MOUSE_WHEEL);
    e.wheel.which = 1;
    e.wheel.y = 1;
    return e;
}

SDL_Event finger(Uint32 type, SDL_TouchID id, float dx = 0, float dy = 0) {
    SDL_Event e = blank(type);
    e.tfinger.touchID = id;
    e.tfinger.dx = dx;
    e.tfinger.dy = dy;
    return e;
}

bool hasAction(const std::vector<Action>& v, const char* n) {
    for (auto& a : v)
        if (a.is(n)) return true;
    return false;
}

}

int inputSelfTest() {
    gFails = 0;
    Input in;
    std::vector<Action> out;
    double t = 1;
    const SDL_JoystickID ps = 1, xbox = 2, nintendo = 3, generic = 4;
    const SDL_TouchID screen = 7, touchpad = 8;
    in.directTouch = [screen](SDL_TouchID id) { return id == screen; };
    in.addTestPad(ps, Source::PlayStation, true);
    in.addTestPad(xbox, Source::Xbox, true);
    in.addTestPad(nintendo, Source::Nintendo, true);
    in.addTestPad(generic, Source::Generic, false);
    auto step = [&](double dt) {
        t += dt;
        in.update(t, out);
    };
    auto send = [&](const SDL_Event& e) {
        out.clear();
        in.handle(e, out);
    };
    step(0);
    check(in.source == Source::Keyboard, "starts on the keyboard");

    send(button(ps, SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(in.source == Source::PlayStation && hasAction(out, "ok"), "DualSense button switches to PlayStation at once and selects");
    send(button(ps, SDL_GAMEPAD_BUTTON_SOUTH, false));
    step(.05);
    send(key(SDLK_RETURN));
    check(in.source == Source::Keyboard && hasAction(out, "ok"), "a key switches to the keyboard 50 ms later");
    send(button(xbox, SDL_GAMEPAD_BUTTON_EAST, true));
    check(in.source == Source::Xbox && hasAction(out, "back"), "Xbox B switches to Xbox and goes back");
    send(button(xbox, SDL_GAMEPAD_BUTTON_EAST, false));

    step(1);
    for (int i = 0; i < 3; i++) send(motion(1, 1));
    check(in.source == Source::Xbox, "6 px of mouse jitter does not leave the pad");
    send(motion(20, 10));
    check(in.source == Source::Mouse, "real mouse travel switches to the mouse");
    check(in.buttonSource == Source::Xbox, "the mouse remembers the last button device");

    send(key(SDLK_ESCAPE));
    send(motion(40, 0));
    check(in.source == Source::Keyboard, "mouse motion right after a key is ignored (hold-off)");
    step(.5);
    send(motion(40, 0));
    check(in.source == Source::Mouse, "mouse motion after the hold-off switches");

    send(key(SDLK_ESCAPE));
    for (int i = 0; i < 4; i++) {
        step(.6);
        send(motion(3, 3));
    }
    check(in.source == Source::Keyboard, "slow mouse drift never adds up");

    send(axis(ps, SDL_GAMEPAD_AXIS_LEFTX, .2f));
    check(in.source == Source::Keyboard && out.empty(), "stick drift (20%) is ignored");
    send(axis(ps, SDL_GAMEPAD_AXIS_LEFTX, .8f));
    check(in.source == Source::PlayStation && hasAction(out, "right"), "a pushed stick switches to the pad and moves");
    send(axis(ps, SDL_GAMEPAD_AXIS_LEFTX, 0));
    send(axis(xbox, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, .9f));
    check(in.source == Source::Xbox && hasAction(out, "r2"), "a pulled trigger switches pads");
    send(axis(xbox, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0));

    send(click());
    check(in.source == Source::Mouse, "a mouse click switches at once");
    send(key(SDLK_ESCAPE));
    send(wheel());
    check(in.source == Source::Mouse, "the wheel switches at once");

    send(finger(SDL_EVENT_FINGER_DOWN, screen));
    check(in.source == Source::Touch, "a touchscreen tap switches to touch");
    send(key(SDLK_ESCAPE));
    step(1);
    send(finger(SDL_EVENT_FINGER_DOWN, touchpad));
    check(in.source == Source::Keyboard, "a finger resting on the touchpad does not switch");
    send(finger(SDL_EVENT_FINGER_UP, touchpad));
    check(in.source == Source::Mouse, "a touchpad tap switches to the mouse");
    send(key(SDLK_ESCAPE));
    step(1);
    send(finger(SDL_EVENT_FINGER_MOTION, touchpad, .03f, .01f));
    check(in.source == Source::Mouse, "moving on the touchpad switches to the mouse");

    send(button(nintendo, SDL_GAMEPAD_BUTTON_EAST, true));
    check(in.source == Source::Nintendo && hasAction(out, "ok"), "Nintendo A (right button) selects");
    send(button(nintendo, SDL_GAMEPAD_BUTTON_EAST, false));
    send(button(nintendo, SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(hasAction(out, "back"), "Nintendo B (bottom button) goes back");
    send(button(nintendo, SDL_GAMEPAD_BUTTON_SOUTH, false));
    in.swapOkBack = true;
    send(button(nintendo, SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(hasAction(out, "ok"), "swap makes Nintendo B select");
    send(button(nintendo, SDL_GAMEPAD_BUTTON_SOUTH, false));
    send(button(xbox, SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(hasAction(out, "back"), "swap makes Xbox A go back");
    send(button(xbox, SDL_GAMEPAD_BUTTON_SOUTH, false));
    in.swapOkBack = false;

    send(button(generic, SDL_GAMEPAD_BUTTON_START, true));
    check(in.source == Source::Generic && out.empty(), "Start on a pad without a home button waits for the release");
    send(button(generic, SDL_GAMEPAD_BUTTON_START, false));
    check(hasAction(out, "options"), "Start alone opens options on release");
    send(button(generic, SDL_GAMEPAD_BUTTON_START, true));
    send(button(generic, SDL_GAMEPAD_BUTTON_BACK, true));
    check(hasAction(out, "ps"), "Start+Select is the home button");
    send(button(generic, SDL_GAMEPAD_BUTTON_START, false));
    bool quiet = out.empty();
    send(button(generic, SDL_GAMEPAD_BUTTON_BACK, false));
    check(quiet && out.empty() && in.takeChordEdge() == 0, "releasing the chord does not open options or create");
    check(!in.padGuide(), "the last pad reports no home button");

    in.inGame = true;
    send(button(generic, SDL_GAMEPAD_BUTTON_BACK, true));
    send(button(generic, SDL_GAMEPAD_BUTTON_START, true));
    step(.2);
    check(out.empty() && in.takeChordEdge() == 0, "in a game a short Start+Select does nothing");
    step(.4);
    check(in.takeChordEdge() == 1, "in a game Start+Select held 0.5 s presses home");
    send(button(generic, SDL_GAMEPAD_BUTTON_START, false));
    check(in.takeChordEdge() == -1, "letting go releases home");
    send(button(generic, SDL_GAMEPAD_BUTTON_BACK, false));
    check(in.takeChordEdge() == 0, "one release only");
    in.inGame = false;

    send(button(xbox, SDL_GAMEPAD_BUTTON_GUIDE, true));
    check(in.source == Source::Xbox && hasAction(out, "ps") && in.padGuide(), "the Xbox button is the home button");
    send(button(xbox, SDL_GAMEPAD_BUTTON_GUIDE, false));

    std::string map = fallbackPadMapping(12, 4, 1);
    check(map.find("a:b2,") != std::string::npos && map.find("dpup:h0.1") != std::string::npos && map.find("start:b9") != std::string::npos,
          "positional mapping for a 12-button pad");
    check(fallbackPadMapping(2, 2, 0).empty(), "a 2-button stick gets no gamepad mapping");
    check(padFamily(SDL_GAMEPAD_TYPE_STANDARD, 0x045e, "") == Source::Xbox, "vendor 045e is Xbox");
    check(padFamily(SDL_GAMEPAD_TYPE_STANDARD, 0x0f0d, "Xbox 360 Fight Pad") == Source::Xbox, "an Xbox name is Xbox");
    check(padFamily(SDL_GAMEPAD_TYPE_PS5, 0x054c, "") == Source::PlayStation, "DualSense is PlayStation");
    check(padFamily(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, "") == Source::Nintendo, "vendor 057e is Nintendo");
    check(padFamily(SDL_GAMEPAD_TYPE_STANDARD, 0x1234, "Generic USB Gamepad") == Source::Generic, "an unknown pad is generic");

    std::printf("%s: input source self-test, %d failure(s)\n", gFails ? "FAILED" : "OK", gFails);
    return gFails ? 1 : 0;
}

}
