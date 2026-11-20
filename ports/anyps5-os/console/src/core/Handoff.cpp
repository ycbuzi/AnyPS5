#include "core/Anim.h"
#include "core/App.h"
#include "core/Glyphs.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "platform/Platform.h"

namespace ui {

static constexpr double kHoldSeconds = 1.0;
static constexpr double kNoGameSeconds = 20.0;
static constexpr double kSuspendAnswerSeconds = 5.0;
static constexpr double kRetrySeconds = 2.0;

static std::string playedFor(double s) {
    long long n = (long long)(s + .5);
    if (n < 60) return std::to_string(n) + " s";
    if (n < 3600) return std::to_string(n / 60) + " min " + std::to_string(n % 60) + " s";
    return std::to_string(n / 3600) + " h " + std::to_string(n % 3600 / 60) + " min";
}

static int displayMaster(SDL_Window* w) {
    SDL_PropertiesID p = SDL_GetWindowProperties(w);
    return platform::drmMaster((int)SDL_GetNumberProperty(p, SDL_PROP_WINDOW_KMSDRM_DRM_FD_NUMBER, -1));
}

bool App::openDisplay() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_Log("video: %s", SDL_GetError());
        return false;
    }
    int w = opt_.w, h = opt_.h;
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!opt_.offscreen && !opt_.windowed) {
        flags |= SDL_WINDOW_FULLSCREEN;
        if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
            w = dm->w;
            h = dm->h;
        }
    }
    window = SDL_CreateWindow("AnyPS5", w, h, flags);
    renderer = window ? SDL_CreateRenderer(window, opt_.offscreen ? "software" : nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("%s: %s", window ? "renderer" : "window", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    if (!opt_.offscreen) SDL_SetRenderVSync(renderer, 1);
    if (!opt_.offscreen && !opt_.windowed) SDL_HideCursor();
    int ow = w, oh = h;
    SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
    theme.resize(ow, oh);
    pointer::init(window, renderer, opt_.offscreen || !opt_.windowed);
    if (!opt_.offscreen) input.overview = platform::overviewKey();
    if (draw_) draw_->reset(renderer);
    else draw_ = std::make_unique<Draw>(renderer);
    images::setRenderer(renderer);
    SDL_Log("display: %s %dx%d, drm master %d", SDL_GetCurrentVideoDriver(), ow, oh, displayMaster(window));
    return true;
}

void App::closeDisplay() {
    input.setTyping(false, window);
    input.forgetWindow();
    images::setRenderer(nullptr);
    text::dropTextures();
    if (draw_) draw_->reset(nullptr);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void App::releaseDisplay() {
    if (!headless_) {
        Uint64 t0 = SDL_GetTicks();
        closeDisplay();
        headless_ = true;
        input.inGame = true;
        releasedAt_ = now();
        psDownAt_ = -1;
        int keyboards = platform::hotkeysOpen();
        SDL_Log("display released for a game in %u ms (%d keyboard(s) watched for Ctrl+Alt+Esc, Ctrl+Alt+Home and the Overview key)", (unsigned)(SDL_GetTicks() - t0), keyboards);
    }
    host.send(json{{"display", "released"}});
}

void App::finishAcquire() {
    headless_ = false;
    input.inGame = false;
    psDownAt_ = -1;
    platform::hotkeysClose();
    SDL_Log("display taken back");
    if (!handoffGame_ && !handoffTitle_.empty()) {
        std::string ran;
        if (gameStartedAt_ >= 0) {
            double end = gameEndedAt_ >= gameStartedAt_ ? gameEndedAt_ : clockSeconds();
            ran = playedFor(end - gameStartedAt_);
            SDL_Log("game ran for %s", ran.c_str());
        }
        std::string after = ran.empty() ? "" : " after " + ran;
        if (closeAsked_ && noSuspend_) toast(handoffTitle_ + " closed" + after + ". AnyPS5 OS cannot show the control center over a running game yet, so " + glyph::name("ps") + " closes the game.");
        else if (closeAsked_) toast(handoffTitle_ + " closed" + after);
        else if (!ran.empty()) toast(handoffTitle_ + " ran for " + ran);
    }
    if (!handoffGame_) gameStartedAt_ = gameEndedAt_ = -1;
    if (!handoffGame_) {
        closeAsked_ = noSuspend_ = suspendAsked_ = false;
        handoffTitle_.clear();
    }
}

void App::askSuspend() {
    if (!handoffGame_ || suspendAsked_ || closeAsked_) return;
    suspendAsked_ = true;
    suspendAskedAt_ = now();
    host.send(json{{"suspend", true}});
}

void App::askClose() {
    if (!handoffGame_ || closeAsked_) return;
    closeAsked_ = true;
    host.send(json{{"closegame", true}});
}

void App::psDown() {
    if (!handoffGame_ || suspendAsked_ || closeAsked_) return;
    psDownAt_ = now();
    psHoldFired_ = false;
}

void App::psUp() {
    if (psDownAt_ < 0) return;
    psDownAt_ = -1;
    if (!psHoldFired_) askSuspend();
}

void App::handoffEvent(const SDL_Event& e) {
    std::vector<Action> ignored;
    input.handle(e, ignored);
    if ((e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) && e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) psDown();
        else psUp();
    }
}

void App::handoffChord() {
    std::vector<Action> ignored;
    input.update(now(), ignored);
    for (int edge = input.takeChordEdge(); edge != 0; edge = input.takeChordEdge()) {
        SDL_Log("PS chord Start+Select %s", edge > 0 ? "down" : "up");
        if (edge > 0) psDown();
        else psUp();
    }
}

void App::handoffAction(const Action& a) {
    if (a.is("ps")) askSuspend();
    else if (a.is("pshold")) askClose();
}

void App::handoffMessage(const json& m) {
    if (m.contains("display") && m["display"].is_string()) {
        std::string d = m["display"].get<std::string>();
        if (d == "release") want_ = Want::Release;
        else if (d == "acquire" && headless_) want_ = Want::Acquire;
    }
    if (m.value("host", false) && m.value("handoff", false) && !handoffGame_ && !headless_ && m.contains("playing") &&
        m["playing"].is_string() && !m.value("suspended", false)) {
        ingame = true;
        handoffGame_ = true;
        handoffTitle_ = m["playing"].get<std::string>();
        want_ = Want::Release;
    }
    if (m.contains("ingame") && m["ingame"].is_boolean()) {
        if (m["ingame"].get<bool>() && m.value("handoff", false)) {
            handoffGame_ = true;
            suspendAsked_ = closeAsked_ = noSuspend_ = false;
            handoffTitle_ = m.value("title", std::string());
            gameStartedAt_ = clockSeconds();
            gameEndedAt_ = -1;
            if (!headless_) want_ = Want::Release;
        } else if (!m["ingame"].get<bool>() && handoffGame_) {
            handoffGame_ = false;
            gameEndedAt_ = clockSeconds();
            suspendAsked_ = false;
            H["playing"] = nullptr;
            H["suspended"] = false;
            if (headless_) want_ = Want::Acquire;
            else finishAcquire();
        }
    }
    if (m.contains("suspended") && m["suspended"].is_boolean()) {
        bool on = m["suspended"].get<bool>();
        H["suspended"] = on;
        if (on && headless_ && handoffGame_) want_ = Want::AcquireSuspended;
        if (!on) suspendAsked_ = false;
    }
}

void App::handoffStep() {
    if (headless_) {
        host.pollUi([this](const json& m) { onMessage(m); });
        int k = platform::hotkeysPoll();
        if (k != 0) SDL_Log("PS key %s %s", platform::hotkeysLast(), k == 1 ? "down" : k == 2 ? "tap" : "up");
        if (k == 1 || k == 2) psDown();
        if (k == 2 || k == -1) psUp();
        handoffChord();
        if (psDownAt_ >= 0 && !psHoldFired_ && now() - psDownAt_ >= kHoldSeconds) {
            psHoldFired_ = true;
            askClose();
        }
        if (suspendAsked_ && !H.value("suspended", false) && now() - suspendAskedAt_ > kSuspendAnswerSeconds) suspendAsked_ = false;
        if (want_ == Want::None && !ingame && !handoffGame_ && now() - releasedAt_ > kNoGameSeconds) {
            SDL_Log("no game started, taking the display back");
            want_ = Want::Acquire;
        }
    }
    Want w = want_;
    if (w == Want::None) return;
    if (w == Want::Release) {
        if (releaseAskedAt_ < 0) releaseAskedAt_ = now();
        if (!headless_ && launchHoldsRelease()) return;
        want_ = Want::None;
        releaseAskedAt_ = -1;
        if (launch_.on) {
            SDL_Log("launch card: shown %.0f ms, handing the display to %s (pid %lld)", (now() - launch_.shownAt) * 1000.0,
                    launch_.title.c_str(), launch_.pid);
            launch_.on = false;
        }
        releaseDisplay();
        return;
    }
    if (!headless_) {
        want_ = Want::None;
        return;
    }
    if (w == Want::AcquireSuspended) {
        want_ = Want::None;
        bool ok = openDisplay();
        if (ok && displayMaster(window) != 0) {
            finishAcquire();
            open("cc");
            return;
        }
        SDL_Log("the stopped game keeps the display (DRM master); closing it instead");
        if (ok) closeDisplay();
        noSuspend_ = true;
        closeAsked_ = true;
        host.send(json{{"closegame", true}});
        return;
    }
    if (now() < retryAt_) return;
    if (!openDisplay()) {
        retryAt_ = now() + kRetrySeconds;
        return;
    }
    want_ = Want::None;
    finishAcquire();
}

}
