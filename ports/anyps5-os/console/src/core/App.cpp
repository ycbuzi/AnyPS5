#include "core/App.h"
#include "core/Anim.h"
#include "core/Glyphs.h"
#include "core/Images.h"
#include "core/Pointer.h"
#include "core/Text.h"
#include "screens/collection.h"
#include "screens/settings_a11yfx.h"
#include "platform/Platform.h"
#include <SDL3_image/SDL_image.h>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <sstream>
#include <thread>

namespace ui {

static App* gApp = nullptr;
App& app() { return *gApp; }

static std::string findAssets() {
    const char* base = SDL_GetBasePath();
    if (base) {
        std::string p = std::string(base) + "assets";
        if (platform::fileExists(p + "/logo-mark.png")) return p;
    }
    return ANYPS5_SOURCE_ASSETS;
}

static std::time_t parseNow(const std::string& s) {
    std::tm tm{};
    int Y, M, D, h = 0, m = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 3) return 0;
    tm.tm_year = Y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

std::time_t App::wallNow() const {
    if (fixedNow_) return fixedNow_ + (std::time_t)now();
    return std::time(nullptr);
}

std::string App::clockText() const {
    std::time_t t = wallNow();
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    if (settings.clock24()) std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else std::snprintf(buf, sizeof buf, "%d:%02d %s", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

std::string App::meUid() const { return me.is_object() ? me.value("uid", "") : ""; }

Screen* App::open(const std::string& id) {
    if (!Router::has(id)) {
        SDL_Log("screen '%s' is not built yet", id.c_str());
        return nullptr;
    }
    return router.open(id);
}

void App::enterHome(const json& profile) {
    me = profile;
    router.go("home");
}

void App::saveSettings() { settings.save(host); }

void App::applySettings() {
    theme.reduceMotion = settings.a11y("motion");
    theme.boldText = settings.a11y("bold");
    theme.highContrast = settings.a11y("contrast");
    setReduceMotion(theme.reduceMotion);
    text::setBoldAll(theme.boldText);
    input.swapOkBack = settings.a11y("swap");
    json hold = settings.a11yv("hold", "normal");
    input.setHoldSpeed(hold.is_string() ? hold.get<std::string>() : "normal");
    json pop = settings.noteOpt("popups", true), secs = settings.noteOpt("time", 3);
    toasts.popups = !pop.is_boolean() || pop.get<bool>();
    toasts.seconds = secs.is_number() ? secs.get<double>() : 3.0;
    if (me.is_object()) {
        if (json* p = settings.profileByUid(me.value("uid", ""))) me = *p;
        else if (json* q = settings.profileByName(me.value("name", ""))) me = *q;
    }
}

void App::reloadLibrary() {
    host.get("/library.js", [this](int status, const std::string& body) {
        if (status == 200 && library.parseJs(body)) router.host(json{{"libraryLoaded", true}});
    });
}

void App::reloadMedia() {
    host.get("/cache/media.js", [this](int status, const std::string& body) {
        if (status != 200) return;
        json m = host::parseJsAssignment(body);
        if (m.is_object()) {
            media = m;
            router.host(json{{"mediaLoaded", true}});
        }
    });
}

std::string App::playing() const {
    auto it = H.find("playing");
    return it != H.end() && it->is_string() ? it->get<std::string>() : std::string();
}

const host::Game* App::playingGame() const {
    std::string p = playing();
    return p.empty() ? nullptr : library.byExeName(p);
}

void App::play(const host::Game& g) {
    if (g.exe.empty() && g.raw.value("installed", true) == false) {
        ui::collection::install(g.id);
        toast("Installing " + g.title + " from your collection. It appears in Downloads, and Play starts it when it is ready.");
        return;
    }
    bool running = g.raw.value("running", false);
    if (running) {
        host.send(json{{"resume", true}});
        return;
    }
    if (!playing().empty()) {
        toast(playing() + " is still running. Press " + glyph::name("ps") + " and choose Close game first.");
        return;
    }
    std::string legacy;
    for (auto& p : settings.profiles())
        if (!settings.isTestUser(p)) {
            legacy = p.value("uid", "");
            break;
        }
    std::string user = me.is_object() && !settings.isTestUser(me) ? me.value("uid", "") : "";
    host.send(json{{"play", g.folder}, {"user", user}, {"legacy", legacy}});
    launchBegin(g.title, g.id);
}

void App::onMessage(const json& m) {
    if (!m.is_object()) return;
    if (m.contains("toast") && m["toast"].is_string()) toast(m["toast"].get<std::string>());
    if (m.contains("screenshot") && m["screenshot"].is_string()) pendingShot_ = m["screenshot"].get<std::string>();
    if (m.contains("open") && m["open"].is_string() && me.is_object()) open(m["open"].get<std::string>());
    if (m.contains("osd") && m["osd"].is_object()) {
        const json& o = m["osd"];
        osd.show(o.value("kind", std::string("volume")), o.value("level", 0), o.value("muted", false));
    }
    if (m.value("host", false)) {
        for (auto it = m.begin(); it != m.end(); ++it) H[it.key()] = it.value();
        if (!settingsLoaded_) {
            settingsLoaded_ = true;
            if (m.contains("settings") && settings.load(m["settings"])) {
            } else {
                saveSettings();
            }
            settings.ensureUids(host);
            applySettings();
        }
    } else if (m.contains("paused") && m["paused"].is_boolean()) {
        H["paused"] = m["paused"];
    }
    if (m.contains("settings") && !m.value("host", false) && settings.load(m["settings"])) applySettings();
    if (m.value("library", false)) reloadLibrary();
    if (m.value("mediaChanged", false)) reloadMedia();
    if (m.contains("trophy") && m["trophy"].is_object()) {
        const json& t = m["trophy"];
        toast("Trophy earned: " + t.value("name", std::string("a new trophy")), t.value("title", std::string("Trophies")), t.value("icon", std::string()));
    }
    if (m.contains("ingame") && m["ingame"].is_boolean()) {
        ingame = m["ingame"].get<bool>();
        if (ingame) {
            if (m.contains("title") && m["title"].is_string()) H["playing"] = m["title"];
            if (!m.value("handoff", false)) open(m.value("open", std::string()) == "create" ? "create" : "cc");
        }
    }
    if (m.contains("launch")) launchMessage(m["launch"]);
    handoffMessage(m);
    router.host(m);
    if (m.value("ps", false)) {
        if (headless_) handoffAction({"ps", "", false, true});
        else globalAction({"ps", "", false, true});
    }
}

void App::globalAction(const Action& a) {
    if (launchAction(a)) return;
    if (a.is("fullscreen")) {
        bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
        SDL_SetWindowFullscreen(window, !fs);
        return;
    }
    if (router.action(a)) return;
    if (a11yfx::action(a)) return;
    std::string b = router.baseId();
    bool loggedIn = me.is_object() && b != "boot" && b != "users";
    if (a.is("ps") && loggedIn) open("cc");
    else if (a.is("help") && b != "boot") open("help");
    else if (a.is("create") && loggedIn) open("create");
}

void App::frame(Draw& d, double dt) {
    host.pollUi([this](const json& m) { onMessage(m); });
    if (deterministic) images::waitIdle();
    else images::pump();
    std::vector<Action> acts;
    input.update(now(), acts);
    for (auto& a : acts) globalAction(a);
    Screen* top = router.top();
    input.setTyping(top && top->typing(), window);
    router.update(dt);
    toasts.update();
    launchStep();
    bool covered = launch_.on && now() - launch_.shownAt > .3;
    d.clear(col::page);
    pointer::beginFrame(d);
    if (!covered) {
        router.draw(d);
        drawLegend(d);
    }
    if (launch_.on) drawLaunch(d);
    toasts.draw(d);
    osd.draw(d);
    std::vector<Action> pacts;
    pointer::endFrame(pacts);
    for (auto& a : pacts) globalAction(a);
    pointer::drawCursor(d);
    text::endFrame();
}

static std::vector<Action> parseKeys(const std::string& keys) {
    std::vector<Action> out;
    std::stringstream ss(keys);
    std::string item;
    while (std::getline(ss, item, ',')) {
        while (!item.empty() && item.front() == ' ') item.erase(0, 1);
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;
        if (item.rfind("move:", 0) == 0 || item.rfind("click:", 0) == 0) {
            std::string y;
            if (std::getline(ss, y, ',')) {
                bool click = item[0] == 'c';
                out.push_back({click ? "pointer.click" : "pointer.move", item.substr(click ? 6 : 5) + "," + y});
            }
        } else if (item.rfind("wheel:", 0) == 0) out.push_back({"pointer.wheel", item.substr(6)});
        else if (item.rfind("text:", 0) == 0) out.push_back({"text", item.substr(5)});
        else if (item.rfind("event:", 0) == 0) out.push_back({"event", item.substr(6)});
        else if (item.rfind("wait", 0) == 0) out.push_back({"wait", item.size() > 5 ? item.substr(5) : ""});
        else if (item.rfind("src:", 0) == 0) out.push_back({"source", item.substr(4)});
        else if (item.rfind("pad:", 0) == 0) out.push_back({item.substr(4), "", false, true});
        else out.push_back({item, ""});
    }
    return out;
}

void App::forceSource(const std::string& id) {
    Source s = Source::Keyboard;
    input.overview = id == "chromebook";
    if (id == "generic-chord") {
        input.addTestPad(9001, Source::Generic, false);
        s = Source::Generic;
    } else if (id != "chromebook" && !sourceFromId(id, s)) {
        SDL_Log("unknown input source '%s'", id.c_str());
        return;
    }
    input.setSource(s);
}

int App::run(const Options& o) {
    gApp = this;
    opt_ = o;
    device = o.device;
    deterministic = o.offscreen;
    if (o.offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    else platform::preferVideoDriver();
    if (!SDL_Init(0)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!text::init()) {
        SDL_Log("TTF_Init: %s", SDL_GetError());
        return 1;
    }
    if (!openDisplay()) return 1;
    if (!o.offscreen || o.pads) SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    platform::netInit();

    if (!o.hostFile.empty()) {
        if (!host.loadFixture(o.hostFile)) {
            SDL_Log("cannot read fixture %s", o.hostFile.c_str());
            return 1;
        }
        fixedNow_ = parseNow(host.fixture().value("now", std::string()));
    } else {
        host.startLive(o.hostUrl);
    }
    if (o.offscreen && !fixedNow_) fixedNow_ = parseNow("2026-01-01T12:00:00");
    images::init(renderer, [this](const std::string& u) { return host.fetchBytes(u); }, findAssets());
    setNow(0);
    reloadLibrary();
    reloadMedia();
    router.go(o.start.empty() ? "boot" : o.start, true);

    auto script = parseKeys(o.keys);
    size_t next = 0;
    double nextAt = o.bootMs / 1000.0;
    double endAt = -1;
    double t = 0;
    bool sourceForced = false;
    auto last = std::chrono::steady_clock::now();
    auto started = last;
    while (running_) {
        SDL_Event e;
        std::vector<Action> acts;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running_ = false;
            else if (headless_) handoffEvent(e);
            else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) theme.resize(e.window.data1, e.window.data2);
            else if (!o.offscreen) {
                input.handle(e, acts);
                pointer::handle(e, acts);
            } else if (o.pads && e.type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && e.type < SDL_EVENT_FINGER_DOWN) {
                input.handle(e, acts);
            }
        }
        if (o.pads)
            for (auto& a : acts) SDL_Log("pads: action %s from %s", a.name.c_str(), sourceId(input.source));
        double dt;
        if (o.offscreen) {
            dt = 1.0 / 60.0;
            if (o.pads) std::this_thread::sleep_until(started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(t)));
        } else {
            auto n = std::chrono::steady_clock::now();
            dt = std::chrono::duration<double>(n - last).count();
            last = n;
            dt = std::min(dt, 0.1);
        }
        t += dt;
        setNow(t);
        if (o.offscreen) {
            if (next < script.size() && t >= nextAt) {
                const Action& a = script[next++];
                if (a.name == "wait") nextAt = t + (a.text.empty() ? o.keyMs : std::atoi(a.text.c_str())) / 1000.0;
                else if (a.name == "event") {
                    json later = host.fixture().value("later", json::array());
                    size_t i = (size_t)std::atoi(a.text.c_str());
                    if (later.is_array() && i < later.size()) host.inject(later[i]);
                    nextAt = t + o.keyMs / 1000.0;
                } else if (a.name == "source") {
                    forceSource(a.text);
                    sourceForced = true;
                    nextAt = t + o.keyMs / 1000.0;
                } else if (a.name.rfind("pointer.", 0) == 0) {
                    float px = 0, py = 0;
                    if (!sourceForced) input.setSource(Source::Mouse);
                    if (a.name == "pointer.wheel") pointer::simWheel((float)std::atof(a.text.c_str()));
                    else if (std::sscanf(a.text.c_str(), "%f,%f", &px, &py) == 2) {
                        if (a.name == "pointer.click") pointer::simClick(px, py);
                        else pointer::simMove(px, py);
                    }
                    nextAt = t + o.keyMs / 1000.0;
                } else {
                    if (!sourceForced) input.setSource(!a.pad ? Source::Keyboard : isPad(input.source) ? input.source : Source::PlayStation);
                    pointer::keyUsed();
                    if (a.name == "text") for (char c : a.text) acts.push_back({c == ' ' ? "space" : "text", std::string(1, c)});
                    else acts.push_back(a);
                    nextAt = t + o.keyMs / 1000.0;
                }
            }
            if (next >= script.size() && endAt < 0) endAt = std::max(t, nextAt) + o.settleMs / 1000.0;
        }
        if (headless_) {
            for (auto& a : acts) handoffAction(a);
            handoffStep();
            if (o.offscreen && endAt >= 0 && t >= endAt) running_ = false;
            if (!o.offscreen) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        for (auto& a : acts) globalAction(a);
        handoffStep();
        if (headless_) continue;
        Draw& d = *draw_;
        a11yfx::begin(renderer, theme.w, theme.h);
        frame(d, dt);
        a11yfx::end(renderer);
        if (!pendingShot_.empty()) {
            SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
            bool saved = shot && IMG_SavePNG(shot, pendingShot_.c_str());
            if (shot) SDL_DestroySurface(shot);
            pendingShot_.clear();
            toast(saved ? "Screenshot saved" : "Could not save the screenshot");
        }
        if (o.offscreen && endAt >= 0 && t >= endAt) {
            if (!o.shot.empty()) {
                SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr);
                if (s) {
                    if (!IMG_SavePNG(s, o.shot.c_str())) SDL_Log("save %s: %s", o.shot.c_str(), SDL_GetError());
                    SDL_DestroySurface(s);
                }
            }
            running_ = false;
        }
        SDL_RenderPresent(renderer);
        if (!o.offscreen) {
            auto spent = std::chrono::steady_clock::now() - last;
            auto budget = std::chrono::microseconds(16000);
            if (spent < budget) std::this_thread::sleep_for(budget - spent);
        }
    }
    for (auto& s : host.sentLog()) std::printf("sent %s\n", s.dump().c_str());
    host.stop();
    images::shutdown();
    text::shutdown();
    draw_.reset();
    platform::hotkeysClose();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}


bool App::beginEmbedded(const Options& o, SDL_Surface* target, const std::string& assets) {
    gApp = this;
    opt_ = o;
    device = o.device;
    deterministic = o.offscreen;
    if (!target || !SDL_Init(0)) return false;
    SDL_Log("embedded: sdl ready");
    if (!text::init()) return false;
    SDL_Log("embedded: text ready");
    embeddedTarget_ = target;
    if (o.gpu) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            window = SDL_CreateWindow("AnyPS5", target->w, target->h, SDL_WINDOW_OPENGL);
            renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
        }
        if (!renderer) {
            SDL_Log("embedded: no GPU renderer (%s), drawing in software", SDL_GetError());
            if (window) SDL_DestroyWindow(window);
            window = nullptr;
        }
    }
    if (!renderer) renderer = SDL_CreateSoftwareRenderer(target);
    if (!renderer) {
        SDL_Log("software renderer: %s", SDL_GetError());
        return false;
    }
    SDL_Log("embedded: %s renderer", SDL_GetRendererName(renderer));
    theme.resize(target->w, target->h);
    SDL_Log("embedded: theme");
    pointer::init(nullptr, renderer, true);
    SDL_Log("embedded: pointer");
    draw_ = std::make_unique<Draw>(renderer);
    images::setRenderer(renderer);
    SDL_Log("embedded: renderer ready");
    platform::netInit();
    if (!o.hostFile.empty()) {
        if (!host.loadFixture(o.hostFile)) return false;
        fixedNow_ = parseNow(host.fixture().value("now", std::string()));
    } else {
        host.startLive(o.hostUrl);
    }
    if (o.offscreen && !fixedNow_) fixedNow_ = parseNow("2026-01-01T12:00:00");
    images::init(renderer, [this](const std::string& u) { return host.fetchBytes(u); }, assets.empty() ? findAssets() : assets);
    SDL_Log("embedded: images ready");
    embeddedT_ = 0;
    setNow(0);
    reloadLibrary();
    reloadMedia();
    SDL_Log("embedded: library ready");
    router.go(o.start.empty() ? "boot" : o.start, true);
    return true;
}

void App::endEmbedded() {
    host.stop();
    images::shutdown();
    text::shutdown();
    draw_.reset();
    if (renderer) SDL_DestroyRenderer(renderer);
    renderer = nullptr;
    if (window) SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}

void App::stepEmbedded(double dt, const std::vector<Action>& acts) {
    embeddedT_ += std::min(std::max(dt, 0.0), 0.1);
    setNow(embeddedT_);
    if (!acts.empty() && acts.front().pad && !isPad(input.source)) input.setSource(quest() ? Source::Quest : Source::PlayStation);
    for (auto& a : acts) {
        float px = 0, py = 0;
        if (a.name == "pointer.wheel") pointer::simWheel((float)std::atof(a.text.c_str()));
        else if ((a.name == "pointer.move" || a.name == "pointer.click") && std::sscanf(a.text.c_str(), "%f,%f", &px, &py) == 2) {
            if (a.name == "pointer.click") pointer::simClick(px, py);
            else pointer::simMove(px, py);
        } else globalAction(a);
    }
    handoffStep();
    Uint64 t0 = SDL_GetTicksNS();
    a11yfx::begin(renderer, theme.w, theme.h);
    frame(*draw_, dt);
    a11yfx::end(renderer);
    Uint64 t1 = SDL_GetTicksNS(), t2 = t1, t3 = t1;
    if (window && embeddedTarget_) {
        if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
            t2 = SDL_GetTicksNS();
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(s, nullptr, embeddedTarget_, nullptr);
            t3 = SDL_GetTicksNS();
            static int logged = 0;
            if (logged++ % 120 == 0) SDL_Log("embedded: frame %.1f ms, read back %.1f ms, copy %.1f ms (%s to %s)", (t1 - t0) / 1e6, (t2 - t1) / 1e6, (t3 - t2) / 1e6, SDL_GetPixelFormatName(s->format), SDL_GetPixelFormatName(embeddedTarget_->format));
            SDL_DestroySurface(s);
        }
    }
    SDL_RenderPresent(renderer);
}

}
