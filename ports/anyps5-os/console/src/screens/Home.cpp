#include "screens/Home.h"
#include "core/Text.h"

namespace ui {

static constexpr float kSmall = 6.5f, kBig = 10.94f, kGap = .65f, kRowLeft = 10.5f;

std::vector<HomeItem> Home::gameItems() const {
    App& A = app();
    bool showNoArt = A.settings.get("showNoArt", false) == true;
    auto view = A.library.recent(showNoArt);
    if (view.size() > 12) view.resize(12);
    std::vector<HomeItem> out;
    const host::Game* pg = A.playingGame();
    std::string playing = A.playing();
    if (!playing.empty()) {
        HomeItem it;
        it.running = true;
        if (pg) it.game = *pg;
        else it.game.title = playing;
        it.game.raw["running"] = true;
        it.title = it.game.title;
        out.push_back(it);
    }
    for (auto& g : view) {
        if (pg && !g.id.empty() && g.id == pg->id) continue;
        HomeItem it;
        it.game = g;
        it.title = g.title;
        out.push_back(it);
    }
    HomeItem lib;
    lib.lib = true;
    lib.title = "Game Library";
    out.push_back(lib);
    HomeItem coll;
    coll.lib = true;
    coll.title = "Your Collection";
    coll.game.raw = json{{"open", "collection"}, {"icon", "box"}};
    out.push_back(coll);
    return out;
}

std::vector<HomeItem> Home::mediaItems() const {
    App& A = app();
    auto all = A.library.all;
    auto recent = all;
    std::stable_sort(recent.begin(), recent.end(), [](const host::Game& a, const host::Game& b) { return a.lastRun > b.lastRun; });
    HomeItem gallery, art, box;
    gallery.media = "gallery";
    gallery.title = "Media Gallery";
    gallery.desc = "Your screenshots and trophy captures, newest first. Select to open the Media Gallery.";
    for (auto& m : A.media.value("items", json::array())) {
        if (!m.is_object()) continue;
        gallery.cards.push_back({m.value("src", std::string()), m.value("thumb", std::string()), m.value("game", std::string()), m.value("sub", std::string()), m.value("id", std::string())});
    }
    art.media = "art";
    art.title = "Key Art";
    art.desc = "Each game's own background art, as it ships with the game.";
    for (auto& g : recent) if (!g.bg.empty()) art.cards.push_back({g.bg, "", g.title, "Key art", ""});
    box.media = "box";
    box.title = "Box Art";
    box.desc = "The cover art of every game in your library.";
    std::stable_sort(all.begin(), all.end(), [](const host::Game& a, const host::Game& b) { return a.title < b.title; });
    for (auto& g : all) if (!g.icon.empty()) box.cards.push_back({g.icon, "", g.title, "Box art", ""});
    return {gallery, art, box};
}

void Home::rebuild() {
    std::string keep = cur() ? (cur()->game.id.empty() ? cur()->title : cur()->game.id) : "";
    items_ = tab_ == "media" ? mediaItems() : gameItems();
    int found = -1;
    for (size_t i = 0; i < items_.size(); i++) {
        const auto& it = items_[i];
        if (!keep.empty() && (it.game.id == keep || (it.game.id.empty() && it.title == keep))) found = (int)i;
    }
    if (found >= 0) sel_ = found;
    if (sel_ >= (int)items_.size()) sel_ = std::max(0, (int)items_.size() - 1);
    size_.resize(items_.size());
}

const HomeItem* Home::cur() const {
    return sel_ >= 0 && sel_ < (int)items_.size() ? &items_[(size_t)sel_] : nullptr;
}

void Home::enter() {
    tab_ = "games";
    focus_ = "row";
    sel_ = 0;
    act_ = 0;
    selGames_ = selMedia_ = 0;
    rebuild();
    size_.assign(items_.size(), Tween{});
    layoutTargets(true);
    heroKey_.clear();
    moved();
}

void Home::host(const json& m) {
    if (!m.value("libraryLoaded", false) && !m.value("mediaLoaded", false) && !m.contains("playing") && !m.value("host", false)) return;
    rebuild();
    layoutTargets(false);
    moved();
}

void Home::setTab(const std::string& t) {
    if (t == tab_) return;
    if (tab_ == "games") selGames_ = sel_;
    else selMedia_ = sel_;
    tab_ = t;
    focus_ = "row";
    act_ = 0;
    items_.clear();
    sel_ = t == "games" ? selGames_ : selMedia_;
    rebuild();
    size_.assign(items_.size(), Tween{});
    layoutTargets(true);
    moved();
}

void Home::layoutTargets(bool instant) {
    const Theme& t = app().theme;
    for (size_t i = 0; i < size_.size(); i++) {
        float v = (int)i == sel_ ? 1.f : 0.f;
        if (instant) size_[i].snap(v);
        else size_[i].set(v, .22f, easeOut);
    }
    const HomeItem* c = cur();
    if (!c) return;
    float nameW = text::width(c->title, {t.r(1.8f), Weight::Regular});
    float left = sel_ * t.r(kSmall + kGap), right = left + t.r(kBig);
    float room = t.W() - t.r(kRowLeft) - t.r(3);
    float shift = std::max(0.f, right + t.r(.75f) + nameW - room);
    float nx = right + t.r(.75f) - shift;
    if (instant) {
        shift_.snap(shift);
        nameX_.snap(nx);
    } else {
        shift_.set(shift, .3f, easeOut);
        nameX_.set(nx, .22f, easeOut);
    }
}

std::string Home::artFor(const HomeItem& it) const {
    if (!it.media.empty()) {
        if (it.cards.empty()) return "";
        if (focus_ == "cards" && act_ < (int)it.cards.size()) return it.cards[(size_t)act_].img;
        return it.cards[0].img;
    }
    if (it.lib) return "";
    return !it.game.bg.empty() ? it.game.bg : it.game.shot;
}

void Home::moved() {
    const HomeItem* c = cur();
    App& A = app();
    A.focusGame = c && !c->lib && c->media.empty() ? c->game.raw : json();
    if (!c) return;
    heroEnter();
    setArt(artFor(*c));
    const Theme& t = app().theme;
    float cardShift = 0;
    if (!c->media.empty() && focus_ == "cards") {
        float right = (act_ + 1) * t.r(14.9f) + act_ * t.r(1.95f);
        cardShift = std::max(0.f, right - (t.W() - t.r(10.9f) - t.r(3)));
    }
    cardShift_.set(cardShift, .3f, easeOut);
}

void Home::heroEnter() {
    const HomeItem* c = cur();
    std::string key = c ? (!c->game.id.empty() ? c->game.id : !c->media.empty() ? c->media : c->title) : "";
    if (key == heroKey_) return;
    heroKey_ = key;
    heroT_.restart(0, 1, .45f, easeOut);
    metaT_.restart(0, 1, .45f, easeOut, .06f);
}

void Home::setArt(const std::string& src) {
    ArtLayer& on = art_[artOn_];
    if (on.src == src && on.op.to == 1) return;
    int nextI = 1 - artOn_;
    ArtLayer& nx = art_[nextI];
    nx.src = src;
    nx.op.restart(nx.op.value(), 1, .7f, easeCss);
    nx.zoom.restart(1.06f, 1, 1.6f, easeArt);
    on.op.set(0, .7f, easeCss);
    artOn_ = nextI;
}

void Home::activate() {
    const HomeItem* c = cur();
    if (!c) return;
    App& A = app();
    if (!c->media.empty()) {
        if (c->media == "gallery") {
            A.open("gallery");
            return;
        }
        if (!c->cards.empty()) {
            focus_ = "cards";
            act_ = 0;
            moved();
        }
        return;
    }
    if (c->lib) {
        A.open(c->game.raw.is_object() ? c->game.raw.value("open", "library") : "library");
        return;
    }
    A.play(c->game);
}

void Home::openCard() {
    const HomeItem* c = cur();
    if (!c || c->cards.empty()) return;
    json items = json::array();
    for (auto& m : c->cards) items.push_back(json{{"img", m.img}, {"title", m.title}, {"sub", m.sub}, {"mid", m.id}});
    App& A = app();
    A.viewer = json{{"items", items}, {"index", act_}};
    A.open("viewer");
}

void Home::pickRow(int i) {
    if (i < 0 || i >= (int)items_.size()) return;
    if (focus_ == "row" && sel_ == i) return;
    focus_ = "row";
    act_ = 0;
    sel_ = i;
    layoutTargets(false);
    moved();
}

void Home::pickAct(int i) {
    if (focus_ == "acts" && act_ == i) return;
    focus_ = "acts";
    act_ = i;
    moved();
}

void Home::pickCard(int i) {
    const HomeItem* c = cur();
    if (!c || i < 0 || i >= (int)c->cards.size()) return;
    if (focus_ == "cards" && act_ == i) return;
    focus_ = "cards";
    act_ = i;
    moved();
}

void Home::stepRow(float rows) {
    rowWheel_ += rows;
    int to = sel_, n = (int)items_.size();
    while (rowWheel_ >= 1) {
        rowWheel_ -= 1;
        to = std::min(n - 1, to + 1);
    }
    while (rowWheel_ <= -1) {
        rowWheel_ += 1;
        to = std::max(0, to - 1);
    }
    pickRow(to);
}

void Home::stepCards(float rows) {
    const HomeItem* c = cur();
    if (!c || c->cards.empty()) return;
    cardWheel_ += rows;
    int to = focus_ == "cards" ? act_ : 0, n = (int)c->cards.size();
    while (cardWheel_ >= 1) {
        cardWheel_ -= 1;
        to = std::min(n - 1, to + 1);
    }
    while (cardWheel_ <= -1) {
        cardWheel_ += 1;
        to = std::max(0, to - 1);
    }
    pickCard(to);
}

bool Home::action(const Action& a) {
    App& A = app();
    const std::string& k = a.name;
    if (k == "l1" || k == "r1" || (k == "up" && focus_ == "row")) {
        setTab(tab_ == "games" ? "media" : "games");
        return true;
    }
    if (k == "options") {
        A.open("gamemenu");
        return true;
    }
    if (k == "triangle") {
        A.open("search");
        return true;
    }
    const HomeItem* c = cur();
    int n = (int)items_.size();
    if (focus_ == "row") {
        if (k == "left" && sel_ > 0) sel_--;
        else if (k == "right" && sel_ < n - 1) sel_++;
        else if (k == "down") {
            if (tab_ == "media") {
                if (c && !c->cards.empty()) { focus_ = "cards"; act_ = 0; }
            } else {
                focus_ = "acts";
                act_ = 0;
            }
        } else if (k == "ok" || k == "enter") {
            activate();
            return true;
        } else if (k == "back") {
            if (sel_ > 0) sel_ = 0;
        } else {
            return false;
        }
        layoutTargets(false);
        moved();
        return true;
    }
    if (focus_ == "acts") {
        bool hub = c && !c->lib && tab_ == "games";
        if (k == "left" && act_ > 0) act_--;
        else if (k == "right" && act_ < 1) act_++;
        else if (k == "down" && hub) {
            A.open("hub");
        } else if (k == "up" || k == "back") {
            focus_ = "row";
        } else if (k == "ok" || k == "enter") {
            if (act_ == 0) activate();
            else A.open("gamemenu");
            return true;
        } else {
            return false;
        }
        moved();
        return true;
    }
    if (focus_ == "cards") {
        int cn = c ? (int)c->cards.size() : 0;
        if (k == "left" && act_ > 0) act_--;
        else if (k == "right" && act_ < cn - 1) act_++;
        else if (k == "up" || k == "back") {
            focus_ = tab_ == "media" ? "row" : "acts";
            act_ = 0;
        } else if (k == "ok" || k == "enter") {
            openCard();
            return true;
        } else {
            return false;
        }
        moved();
        return true;
    }
    return false;
}

std::vector<Hint> Home::legend() const {
    return {{"Select", "ok"}, {"Options", "options"}, {"Control center", "ps"}, {"Create", "create"}};
}

ANYPS5_SCREEN(Home, "home");

}
