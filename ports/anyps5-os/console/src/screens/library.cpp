#include "screens/library.h"
#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr int kCols = 5;
constexpr float kTile = 18.44f, kGap = 1.5f, kLeft = 10.8f, kTop = 21.f, kPad = .3f, kBottomPad = 6.f;
}

Rect Library::sortButton() {
    const Theme& t = app().theme;
    float s = t.r(2.8f);
    return {t.r(4.1f), t.r(23.25f) - s / 2, s, s};
}

Rect Library::tileRect(int i, float scroll) const {
    const Theme& t = app().theme;
    float step = t.r(kTile + kGap);
    return {t.r(kLeft + kPad) + (i % kCols) * step, t.r(kTop + kPad) + (i / kCols) * step - scroll, t.r(kTile), t.r(kTile)};
}

const LibEntry* Library::cur() const {
    return sel_ >= 0 && sel_ < (int)list_.size() ? &list_[sel_] : nullptr;
}

std::string Library::countText() const {
    static const char* names[] = {"All", "Installed", "Plays"};
    return std::string(names[tab_]) + ": " + std::to_string(list_.size());
}

void Library::rebuild(bool keep) {
    std::string id = keep && cur() ? cur()->g.id : "";
    std::string title = keep && cur() ? cur()->g.title : "";
    list_ = libraryEntries(tab_, app().settings.get("libSort", "recent").get<std::string>());
    if (keep)
        for (size_t i = 0; i < list_.size(); i++)
            if ((!id.empty() && list_[i].g.id == id) || (id.empty() && list_[i].g.title == title)) sel_ = (int)i;
    sel_ = std::clamp(sel_, 0, std::max(0, (int)list_.size() - 1));
}

void Library::moved(bool instant, bool quiet) {
    const Theme& t = app().theme;
    float step = t.r(kTile + kGap);
    float view = t.H() - t.r(kTop);
    int rows = ((int)list_.size() + kCols - 1) / kCols;
    float content = t.r(kPad * 2 + kBottomPad) + std::max(0, rows) * step - t.r(kGap);
    float top = t.r(kPad) + (sel_ / kCols) * step, bottom = top + t.r(kTile);
    float s = scroll_.to;
    if (top < s) s = top;
    if (bottom > s + view) s = bottom - view;
    s = std::clamp(s, 0.f, std::max(0.f, content - view));
    if (instant) scroll_.snap(s);
    else if (std::fabs(s - scroll_.to) > .5f) scroll_.set(s, .2f, easeOut);
    if (instant) nameOp_.snap(1);
    else if (!quiet) nameOp_.restart(0, 1, .15f, easeCss);
    const LibEntry* e = cur();
    std::string want = e ? (!e->g.bg.empty() ? e->g.bg : e->g.shot) : "";
    if (want != art_[artOn_]) {
        int nx = 1 - artOn_;
        art_[nx] = want;
        if (instant) {
            artOp_[nx].snap(1);
            artOp_[artOn_].snap(0);
        } else {
            artOp_[nx].restart(0, 1, .6f, easeCss);
            artOp_[artOn_].set(0, .6f, easeCss);
        }
        artOn_ = nx;
    }
}

void Library::sorted() {
    sel_ = 0;
    rebuild(false);
    moved(false);
}

void Library::enter() {
    sel_ = 0;
    rebuild(false);
    moved(true);
    collection::refresh();
}

void Library::update(double) {}

void Library::scrollRows(float rows) {
    wheel_ += rows;
    int n = (int)list_.size();
    bool moved_ = false;
    while (wheel_ >= 1) {
        wheel_ -= 1;
        if (sel_ + kCols < n) {
            sel_ += kCols;
            moved_ = true;
        }
    }
    while (wheel_ <= -1) {
        wheel_ += 1;
        if (sel_ >= kCols) {
            sel_ -= kCols;
            moved_ = true;
        }
    }
    if (moved_) moved(false);
}

void Library::host(const json& m) {
    collection::onHost(m, this);
    if (m.value("libraryLoaded", false) || m.value("collectionChanged", false)) {
        rebuild(true);
        moved(false, true);
    }
}

void Library::activate() {
    const LibEntry* e = cur();
    if (!e) return;
    App& A = app();
    if (e->playable()) A.play(e->g);
    else if (e->st == collection::State::Installing) A.toast(e->g.title + " is installing (" + collection::labelFor(e->g.id) + ")", "Downloads", e->g.icon);
    else if (e->st == collection::State::Installed) {
        A.reloadLibrary();
        A.toast(e->g.title + " is installed and shows up in the library soon", "Downloads", e->g.icon);
    } else if (collection::supported() && collection::byId(e->g.id)) collection::act(e->g.id);
    else if (collection::supported()) collection::install(e->g.id);
    else A.toast(e->g.title + " is not installed");
}

void Library::options() {
    const LibEntry* e = cur();
    if (!e) {
        psmenu::show(psmenu::sortItems(), sortButton(), false);
        return;
    }
    psmenu::show(psmenu::forEntry(*e, true), tileRect(sel_, scroll_.value()), true);
}

bool Library::action(const Action& a) {
    const std::string& k = a.name;
    int n = (int)list_.size();
    if (k == "back") {
        app().open("home");
        return true;
    }
    if (k == "options") {
        options();
        return true;
    }
    if (k == "tab" || k == "r1" || k == "l1") {
        tab_ = (tab_ + (k == "l1" ? 2 : 1)) % 3;
        sel_ = 0;
        rebuild(false);
        moved(false);
        return true;
    }
    if (k == "ok" || k == "enter") {
        activate();
        return true;
    }
    if (k == "square") {
        openCollection("library");
        return true;
    }
    if (k == "left" && sel_ % kCols) sel_--;
    else if (k == "right" && sel_ < n - 1 && sel_ % kCols < kCols - 1) sel_++;
    else if (k == "down" && sel_ + kCols < n) sel_ += kCols;
    else if (k == "up" && sel_ >= kCols) sel_ -= kCols;
    else return k == "left" || k == "right" || k == "up" || k == "down";
    moved(false);
    return true;
}

std::vector<Hint> Library::legend() const {
    const LibEntry* e = cur();
    using collection::State;
    std::string first = "Play";
    if (e && !e->playable()) {
        const collection::Item* it = collection::byId(e->g.id);
        std::string verb = it ? collection::actionVerb(*it) : "";
        first = e->st == State::Installing ? "Select" : !verb.empty() && verb != "Cancel" ? verb : e->st == State::NotInstalled ? "Install" : "Select";
    }
    return {{first, "ok"}, {"Back", "back"}, {"Tabs", "r1"}, {"Manage Collection", "square"}, {"Options", "options"}};
}

ANYPS5_SCREEN(Library, "library");

}
