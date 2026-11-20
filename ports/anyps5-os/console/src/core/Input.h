#pragma once
#include "core/InputSource.h"
#include <SDL3/SDL.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui {

struct Action {
    std::string name;
    std::string text;
    bool repeat = false;
    bool pad = false;
    bool is(const char* n) const { return name == n; }
};

class Input {
public:
    void handle(const SDL_Event& e, std::vector<Action>& out);
    void update(double now, std::vector<Action>& out);
    void setTyping(bool on, SDL_Window* w);
    bool typing() const { return typing_; }
    void forgetWindow() { typing_ = false; }
    Source source = Source::Keyboard;
    Source buttonSource = Source::Keyboard;
    bool lastPad = false;
    bool overview = false;
    bool inGame = false;
    bool swapOkBack = false;
    int holdDelayMs = 380;
    int repeatMs = 110;
    void setHoldSpeed(const std::string& speed);
    void setSource(Source s);
    Source padSource() const;
    bool padGuide() const;
    bool okSouth(Source family) const { return (family != Source::Nintendo) != swapOkBack; }
    std::vector<PadInfo> padList() const;
    bool rumble(int vendor, int product, float strength, int ms);
    int takeChordEdge();
    void addTestPad(SDL_JoystickID id, Source family, bool guide);
    std::function<bool(SDL_TouchID)> directTouch;

private:
    struct Pad {
        SDL_Gamepad* g = nullptr;
        PadInfo info;
        bool start = false, back = false, chord = false, chordSent = false, chordUp = false;
        double chordAt = -1;
    };
    void padButton(const std::string& name, bool down, double now, std::vector<Action>& out);
    void firm(Source s);
    void soft(float px);
    void observePointer(const SDL_Event& e);
    bool fromPadPointer(bool touch, Uint64 id);
    bool chordButton(Pad& p, bool start, bool down, std::vector<Action>& out);
    void joystickAdded(SDL_JoystickID id);
    void openPad(SDL_JoystickID id);
    bool typing_ = false;
    std::map<SDL_JoystickID, Pad> pads_;
    std::map<SDL_JoystickID, bool> fallback_;
    std::map<Uint64, bool> padPointer_;
    std::vector<int> chordEdges_;
    SDL_JoystickID lastPadId_ = 0;
    std::map<std::string, bool> held_;
    std::map<std::string, double> nextAt_;
    std::map<std::string, int> holders_;
    double now_ = 0;
    double firmAt_ = -100;
    double travelAt_ = -100;
    float travel_ = 0;
};

int inputSelfTest();

}
