#include "screens/collectionScreen.h"
#include "screens/library.h"
#include "widgets/osk.h"
#include <algorithm>
#include <cmath>

namespace ui {

using collection::Item;
using collection::State;

namespace {
constexpr float kLeft = 10.8f, kListTop = 17.f, kRowH = 7.2f, kRowGap = .6f, kBottom = 6.f;

bool usable(const Item& it) { return it.actionId.empty() || it.actionEnabled; }
}

std::vector<CollectionScreen::Button> CollectionScreen::buttons() const {
    std::string srv = collection::server();
    std::string shown = srv.rfind("http://", 0) == 0 ? srv.substr(7) : srv;
    if (empty()) return {{srv.empty() ? "Set PC Address" : "PC: " + shown, [this] { askServer(); }}, {"Retry", [] { collection::refresh(); }}};
    return {{"Refresh", [] { collection::refresh(); }}, {srv.empty() ? "Connect to PC" : "PC: " + shown, [this] { askServer(); }}};
}

bool CollectionScreen::empty() const { return ids_.empty(); }

const Item* CollectionScreen::cur() const {
    return !onBar_ && sel_ >= 0 && sel_ < (int)ids_.size() ? collection::byId(ids_[sel_]) : nullptr;
}

Rect CollectionScreen::listRect() const {
    const Theme& t = app().theme;
    return {t.r(kLeft - 1), t.r(kListTop - .6f), t.W() - t.r(kLeft * 2 - 2), t.H() - t.r(kListTop - .6f + kBottom)};
}

Rect CollectionScreen::rowRect(int i, float scroll) const {
    const Theme& t = app().theme;
    return {t.r(kLeft), t.r(kListTop) + i * t.r(kRowH + kRowGap) - scroll, t.W() - t.r(kLeft * 2), t.r(kRowH)};
}

void CollectionScreen::rebuild() {
    std::string keep = sel_ >= 0 && sel_ < (int)ids_.size() ? ids_[sel_] : "";
    ids_.clear();
    for (auto& it : collection::items()) ids_.push_back(it.id);
    for (size_t i = 0; i < ids_.size(); i++)
        if (ids_[i] == keep) sel_ = (int)i;
    sel_ = std::clamp(sel_, 0, std::max(0, (int)ids_.size() - 1));
    int nb = (int)buttons().size();
    bar_ = std::clamp(bar_, 0, nb - 1);
    if (ids_.empty()) onBar_ = true;
}

void CollectionScreen::moved(bool instant) {
    const Theme& t = app().theme;
    Rect v = listRect();
    float step = t.r(kRowH + kRowGap);
    float content = ids_.size() * step + t.r(1.2f);
    float top = sel_ * step, bottom = top + t.r(kRowH) + t.r(1.2f);
    float s = scroll_.to;
    if (onBar_) s = 0;
    if (top < s) s = top;
    if (bottom > s + v.h) s = bottom - v.h;
    s = std::clamp(s, 0.f, std::max(0.f, content - v.h));
    if (instant) scroll_.snap(s);
    else if (std::fabs(s - scroll_.to) > .5f) scroll_.set(s, .2f, easeOut);
}

void CollectionScreen::enter() {
    back_ = nextBack_.empty() ? "home" : nextBack_;
    nextBack_.clear();
    sel_ = 0;
    bar_ = 0;
    onBar_ = false;
    rebuild();
    moved(true);
    collection::refresh();
}

void CollectionScreen::host(const json& m) {
    collection::onHost(m, this);
    if (m.value("collectionChanged", false) || m.value("libraryLoaded", false)) {
        bool was = ids_.empty();
        rebuild();
        if (was && !ids_.empty()) onBar_ = false;
        moved(false);
    }
}

void CollectionScreen::askServer() const {
    std::string srv = collection::server();
    if (srv.rfind("http://", 0) == 0) srv = srv.substr(7);
    osk::Opts o;
    o.max = 64;
    o.capFirst = false;
    osk::open("PC library address (IP:port)", srv, [](const std::string& v) {
        if (!v.empty()) collection::setServer(v);
    }, o);
}

void CollectionScreen::activate(int i) {
    if (i < 0 || i >= (int)ids_.size()) return;
    const Item* it = collection::byId(ids_[i]);
    if (!it) return;
    std::string id = it->id, title = it->title.empty() ? it->id : it->title;
    LibEntry e = collectionEntry(*it);
    std::string verb = collection::actionVerb(*it);
    Rect r = rowRect(i, scroll_.value());
    const Theme& t = app().theme;
    Rect anchor{r.x + r.w - t.r(34.4f), r.y, t.r(34.4f), r.h};
    if (it->state == State::Installing) {
        psmenu::show({{"Cancel Install", [id] { collection::cancel(id); }}, {"Keep Installing", [] {}, true}}, anchor, false);
        return;
    }
    if (usable(*it) && (verb == "Install" || verb == "Update" || verb == "Retry")) {
        collection::act(id);
        return;
    }
    if (usable(*it) && verb == "Reinstall") {
        std::vector<MenuItem> m{{"Reinstall " + title, [id] { collection::reinstall(id); }}};
        if (e.playable()) {
            host::Game g = e.g;
            m.push_back({"Play Game", [g] { app().play(g); }});
        }
        m.push_back({"Information", [e] { gameinfo::show(e); }, true});
        m.push_back({"Cancel", [] {}, true});
        psmenu::show(m, anchor, false);
        return;
    }
    gameinfo::show(e);
}

void CollectionScreen::options() {
    const Item* it = cur();
    if (!it) {
        psmenu::show({{"Refresh", [] { collection::refresh(); }}, {"Set PC Address", [this] { askServer(); }}, {"Game Library", [] { app().open("library"); }, true}}, {app().theme.r(kLeft), app().theme.r(11), 1, app().theme.r(3)}, false);
        return;
    }
    Rect r = rowRect(sel_, scroll_.value());
    const Theme& t = app().theme;
    psmenu::show(psmenu::forEntry(collectionEntry(*it), false), {r.x + r.w - t.r(34.4f), r.y, t.r(34.4f), r.h}, false);
}

void CollectionScreen::scrollRows(float rows) {
    wheel_ += rows;
    int n = (int)ids_.size();
    bool moved_ = false;
    while (wheel_ >= 1) {
        wheel_ -= 1;
        if (onBar_ && n) {
            onBar_ = false;
            moved_ = true;
        } else if (sel_ < n - 1) {
            sel_++;
            moved_ = true;
        }
    }
    while (wheel_ <= -1) {
        wheel_ += 1;
        if (!onBar_ && sel_ > 0) {
            sel_--;
            moved_ = true;
        }
    }
    if (moved_) moved(false);
}

bool CollectionScreen::action(const Action& a) {
    const std::string& k = a.name;
    int n = (int)ids_.size();
    int nb = (int)buttons().size();
    if (k == "back") {
        app().open(back_.empty() || back_ == "collection" ? "home" : back_);
        return true;
    }
    if (k == "triangle") {
        collection::refresh();
        return true;
    }
    if (k == "options") {
        options();
        return true;
    }
    if (k == "ok" || k == "enter") {
        if (onBar_) {
            auto b = buttons();
            if (bar_ >= 0 && bar_ < nb && b[bar_].run) b[bar_].run();
        } else {
            activate(sel_);
        }
        return true;
    }
    if (k == "square") {
        if (const Item* it = cur()) gameinfo::show(collectionEntry(*it));
        return true;
    }
    if (onBar_) {
        if (k == "left" && bar_ > 0) bar_--;
        else if (k == "right" && bar_ < nb - 1) bar_++;
        else if (k == "down" && n) onBar_ = false;
        else return k == "left" || k == "right" || k == "up" || k == "down";
    } else {
        if (k == "up" && sel_ > 0) sel_--;
        else if (k == "up") onBar_ = true;
        else if (k == "down" && sel_ < n - 1) sel_++;
        else return k == "left" || k == "right" || k == "down";
    }
    moved(false);
    return true;
}

std::vector<Hint> CollectionScreen::legend() const {
    std::vector<Hint> h;
    const Item* it = cur();
    if (onBar_ || !it) h.push_back({"Select", "ok"});
    else if (it->state == State::Installing) h.push_back({"Cancel", "ok"});
    else {
        std::string verb = collection::actionVerb(*it);
        h.push_back({usable(*it) && !verb.empty() ? verb : "Information", "ok"});
        h.push_back({"Information", "square"});
    }
    h.push_back({"Back", "back"});
    h.push_back({"Refresh", "triangle"});
    h.push_back({"Options", "options"});
    return h;
}

ANYPS5_SCREEN(CollectionScreen, "collection");

void openCollection(const std::string& back) {
    App& A = app();
    if (auto* s = dynamic_cast<CollectionScreen*>(A.router.get("collection"))) s->setBack(back);
    A.open("collection");
}

}
