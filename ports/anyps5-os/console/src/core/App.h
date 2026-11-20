#pragma once
#include "core/Draw.h"
#include "core/Input.h"
#include "core/Router.h"
#include "core/Theme.h"
#include "core/Osd.h"
#include "core/Toast.h"
#include "host/HostClient.h"
#include "host/Library.h"
#include "host/Settings.h"
#include <ctime>
#include <memory>
#include <string>

namespace ui {

struct Options {
    bool offscreen = false;
    bool windowed = false;
    int w = 1920, h = 1080;
    std::string hostFile;
    std::string hostUrl = "http://127.0.0.1:8790";
    std::string keys;
    std::string shot;
    std::string start;
    int bootMs = 2000;
    int keyMs = 400;
    int settleMs = 1700;
    bool pads = false;
    bool gpu = false;
    std::string device;
};

class App {
public:
    Theme theme;
    host::HostClient host;
    host::Settings settings;
    host::Library library;
    json H = json::object();
    std::string device;
    bool quest() const { return device == "quest"; }
    json me;
    json login;
    json focusGame;
    json viewer;
    json media = json::object();
    Router router;
    Toasts toasts;
    Osd osd;
    std::string pendingShot_;
    Input input;
    bool ingame = false;
    bool deterministic = false;
    bool headless() const { return headless_; }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    int run(const Options& o);
    bool beginEmbedded(const Options& o, SDL_Surface* target, const std::string& assets);
    void stepEmbedded(double dt, const std::vector<Action>& acts);
    void endEmbedded();
    void quit() { running_ = false; }
    Screen* open(const std::string& id);
    void enterHome(const json& profile);
    void saveSettings();
    void applySettings();
    void reloadLibrary();
    void reloadMedia();
    void play(const host::Game& g);
    std::string playing() const;
    const host::Game* playingGame() const;
    std::time_t wallNow() const;
    std::string clockText() const;
    std::string meUid() const;
    void act(const Action& a) { globalAction(a); }
    void toast(const std::string& t, const std::string& src = "", const std::string& icon = "") {
        SDL_Log("toast: %s", t.c_str());
        toasts.push(t, src, icon);
    }

private:
    void onMessage(const json& m);
    void globalAction(const Action& a);
    void frame(Draw& d, double dt);
    void drawLegend(Draw& d);
    void forceSource(const std::string& id);
    bool openDisplay();
    void closeDisplay();
    void handoffMessage(const json& m);
    void handoffEvent(const SDL_Event& e);
    void handoffAction(const Action& a);
    void handoffStep();
    void handoffChord();
    void releaseDisplay();
    void acquireDisplay();
    void finishAcquire();
    void psDown();
    void psUp();
    void askSuspend();
    void askClose();
    void launchBegin(const std::string& title, const std::string& titleId);
    void launchMessage(const json& l);
    bool launchAction(const Action& a);
    void launchStep();
    void drawLaunch(Draw& d);
    bool launchHoldsRelease();
    double clockSeconds() const;
    struct LaunchCard {
        bool on = false;
        bool heard = false;
        std::string title, titleId, icon, bg, phase, detail;
        double percent = -1;
        double shownAt = 0, heardAt = 0, lastLog = -1;
        long long pid = 0;
    };
    LaunchCard launch_;
    double gameStartedAt_ = -1;
    double gameEndedAt_ = -1;
    double releaseAskedAt_ = -1;
    enum class Want { None, Release, Acquire, AcquireSuspended };
    Options opt_;
    std::unique_ptr<Draw> draw_;
    bool headless_ = false;
    bool handoffGame_ = false;
    bool suspendAsked_ = false;
    bool closeAsked_ = false;
    bool noSuspend_ = false;
    Want want_ = Want::None;
    double releasedAt_ = 0;
    double retryAt_ = 0;
    double suspendAskedAt_ = 0;
    double psDownAt_ = -1;
    bool psHoldFired_ = false;
    std::string handoffTitle_;
    bool running_ = true;
    bool settingsLoaded_ = false;
    std::time_t fixedNow_ = 0;
    double embeddedT_ = 0;
    SDL_Surface* embeddedTarget_ = nullptr;
};

App& app();

}
