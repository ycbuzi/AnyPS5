#include "core/Anim.h"
#include "core/App.h"
#include "core/Glyphs.h"
#include "core/Images.h"
#include "core/Text.h"
#include <algorithm>
#include <cmath>

namespace ui {

static constexpr double kCardMinSeconds = 2.0;
static constexpr double kCardStartedWaitSeconds = 10.0;
static constexpr double kCardNoHostSeconds = 6.0;
static constexpr double kCardSilentSeconds = 20.0;
static constexpr double kCardCoverSeconds = 0.3;

double App::clockSeconds() const { return deterministic ? now() : SDL_GetTicks() / 1000.0; }

void App::launchBegin(const std::string& title, const std::string& titleId) {
    const host::Game* g = titleId.empty() ? nullptr : library.byId(titleId);
    if (!g)
        for (const host::Game& c : library.all)
            if (c.title == title) {
                g = &c;
                break;
            }
    launch_ = LaunchCard{};
    launch_.on = true;
    launch_.title = title.empty() && g ? g->title : title;
    launch_.titleId = titleId.empty() && g ? g->id : titleId;
    if (g) {
        launch_.icon = g->icon;
        launch_.bg = g->bg.empty() ? g->shot : g->bg;
    }
    launch_.phase = "starting";
    launch_.shownAt = now();
    SDL_Log("launch card: %s shown", launch_.title.c_str());
}

void App::launchMessage(const json& l) {
    if (!l.is_object()) return;
    std::string phase = l.value("phase", std::string());
    std::string title = l.value("title", std::string());
    if (!launch_.on && !headless_ && (phase == "starting" || phase == "preparing" || phase == "started"))
        launchBegin(title, l.value("titleId", std::string()));
    if (!launch_.on) return;
    launch_.heard = true;
    launch_.heardAt = now();
    if (phase == "failed") {
        SDL_Log("launch card: %s did not start, card hidden", launch_.title.c_str());
        launch_.on = false;
        return;
    }
    if (phase.empty()) return;
    launch_.phase = phase;
    if (phase == "preparing") {
        launch_.percent = l.contains("percent") && l["percent"].is_number() ? l["percent"].get<double>() : -1;
        launch_.detail = l.value("detail", std::string());
        if (launch_.lastLog < 0 || now() - launch_.lastLog >= 2.0) {
            launch_.lastLog = now();
            SDL_Log("launch card: preparing %.0f%% (%s)", launch_.percent, launch_.detail.c_str());
        }
    } else if (phase == "started") {
        launch_.pid = l.contains("pid") && l["pid"].is_number_integer() ? l["pid"].get<long long>() : 0;
        SDL_Log("launch card: game process started (pid %lld) %.0f ms after the card appeared", launch_.pid,
                (now() - launch_.shownAt) * 1000.0);
    }
}

bool App::launchAction(const Action& a) {
    if (!launch_.on || a.is("fullscreen")) return false;
    if (a.is("back") && launch_.phase == "preparing") {
        launch_.on = false;
        toast(launch_.title + " starts when it is ready");
    }
    return true;
}

void App::launchStep() {
    if (!launch_.on || headless_) return;
    if (!launch_.heard && now() - launch_.shownAt > kCardNoHostSeconds) {
        SDL_Log("launch card: no answer from the host, card hidden");
        launch_.on = false;
    } else if (launch_.heard && launch_.phase != "preparing" && now() - launch_.heardAt > kCardSilentSeconds) {
        SDL_Log("launch card: host went quiet while %s, card hidden", launch_.phase.c_str());
        launch_.on = false;
    }
}

bool App::launchHoldsRelease() {
    if (!launch_.on) return false;
    if (now() - launch_.shownAt < kCardMinSeconds) return true;
    return launch_.heard && launch_.phase != "started" && now() - releaseAskedAt_ < kCardStartedWaitSeconds;
}

void App::drawLaunch(Draw& d) {
    const Theme& t = theme;
    float fade = (float)std::min(1.0, (now() - launch_.shownAt) / 0.25);
    d.pushAlpha(fade);
    Rect all{0, 0, t.W(), t.H()};
    d.rect(all, col::page);
    if (SDL_Texture* bg = launch_.bg.empty() ? nullptr : images::get(launch_.bg)) d.image(bg, all, Fit::Cover, .38f);
    d.linear(all, 180, {{0.f, black(.25f)}, {.55f, black(.55f)}, {1.f, black(.9f)}});
    float cx = t.W() / 2, is = t.r(13);
    float iy = t.H() * .42f - is / 2;
    Rect ir{cx - is / 2, iy, is, is};
    SDL_Texture* icon = launch_.icon.empty() ? nullptr : images::get(launch_.icon);
    if (icon) d.image(icon, ir, Fit::Cover, 1.f, t.r(1.2f));
    else {
        d.round(ir, t.r(1.2f), hex(0x14182a));
        if (SDL_Texture* logo = images::asset("logo-mark.png")) d.image(logo, {ir.x + is * .22f, ir.y + is * .22f, is * .56f, is * .56f}, Fit::Contain);
    }
    float y = iy + is + t.r(2.2f);
    Font ft{t.r(2.6f), Weight::Semibold};
    text::draw(d, text::ellipsize(launch_.title, ft, t.W() - t.r(8)), cx, y, {ft.size, ft.weight, col::fg, Align::Center, VAlign::Top, t.r(.3f)});
    y += text::lineHeight(ft) + t.r(.9f);
    std::string status = "Starting\xE2\x80\xA6";
    if (launch_.phase == "preparing")
        status = launch_.percent >= 0 ? "Preparing\xE2\x80\xA6 " + std::to_string((int)std::floor(launch_.percent)) + "%" : "Preparing\xE2\x80\xA6";
    Font fs{t.r(1.6f), Weight::Regular};
    text::draw(d, status, cx, y, {fs.size, fs.weight, col::dim, Align::Center, VAlign::Top});
    y += text::lineHeight(fs) + t.r(1.0f);
    if (launch_.phase == "preparing") {
        float bw = std::min(t.r(30), t.W() - t.r(8)), bh = t.r(.5f);
        Rect bar{cx - bw / 2, y, bw, bh};
        d.round(bar, bh / 2, white(.16f));
        if (launch_.percent >= 0) {
            float p = (float)std::clamp(launch_.percent / 100.0, 0.0, 1.0);
            if (p > 0) d.round({bar.x, bar.y, std::max(bh, bw * p), bh}, bh / 2, white(.9f));
        } else {
            float span = bw * .25f;
            float x = bar.x + (bw - span) * (float)(.5 + .5 * std::sin(now() * 2.4));
            d.round({x, bar.y, span, bh}, bh / 2, white(.9f));
        }
        y += bh + t.r(.9f);
        if (!launch_.detail.empty()) {
            Font fd{t.r(1.3f), Weight::Regular};
            text::draw(d, text::ellipsize(launch_.detail, fd, t.W() - t.r(8)), cx, y, {fd.size, fd.weight, col::faint, Align::Center, VAlign::Top});
        }
    }
    Font fq{t.r(1.5f), Weight::Medium}, fk{t.r(1.3f), Weight::Regular};
    float qy = t.H() - t.r(8.6f);
    Source pad = input.padSource();
    bool chord = pad == Source::Generic && !input.padGuide();
    std::string quit = quest() ? "To quit: {ps}" : isPad(input.buttonSource) ? (chord ? "To quit: hold {ps} or press Ctrl+Alt+Esc" : "To quit: {ps} or Ctrl+Alt+Esc")
                                                 : input.overview ? "To quit: {overview} or Ctrl+Alt+Esc" : "To quit: Ctrl+Alt+Esc, or {ps} on a controller";
    glyph::line(d, quit, cx, qy, {fq.size, fq.weight, col::fg, Align::Center, VAlign::Top}, pad);
    qy += text::lineHeight(fq) + t.r(.7f);
    glyph::line(d, isPad(input.buttonSource) && input.overview ? "Chromebook keyboard: the Overview key {overview} (top row, 5th key)" : "Hold it for 1 second to close the game straight away",
                cx, qy, {fk.size, fk.weight, col::dim, Align::Center, VAlign::Top}, pad);
    if (launch_.phase == "preparing") {
        qy += text::lineHeight(fk) + t.r(.7f);
        glyph::line(d, glyph::has("back") ? "{back} hides this, the game starts when it is ready" : "Back: hide this, the game starts when it is ready", cx, qy,
                    {fk.size, fk.weight, col::faint, Align::Center, VAlign::Top});
    }
    d.popAlpha();
}

}
