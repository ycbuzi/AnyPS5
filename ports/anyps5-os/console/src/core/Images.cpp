#include "core/Images.h"
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ui::images {

namespace {

enum class State { Loading, Ready, Failed };

struct Slot {
    State state = State::Loading;
    SDL_Texture* tex = nullptr;
};

SDL_Renderer* gR = nullptr;
Fetch gFetch;
std::string gAssets;
std::unordered_map<std::string, Slot> gSlots;
std::unordered_map<std::string, SDL_Texture*> gIcons;
std::mutex gM;
std::condition_variable gCv, gIdleCv;
std::deque<std::string> gQueue;
std::vector<std::pair<std::string, SDL_Surface*>> gDone;
int gBusy = 0;
bool gStop = false;
std::vector<std::thread> gThreads;

void work() {
    for (;;) {
        std::string url;
        {
            std::unique_lock<std::mutex> l(gM);
            gCv.wait(l, [] { return gStop || !gQueue.empty(); });
            if (gStop) return;
            url = gQueue.front();
            gQueue.pop_front();
            gBusy++;
        }
        SDL_Surface* s = nullptr;
        auto bytes = gFetch ? gFetch(url) : std::nullopt;
        if (bytes && !bytes->empty()) {
            SDL_IOStream* io = SDL_IOFromConstMem(bytes->data(), bytes->size());
            if (io) s = IMG_Load_IO(io, true);
        }
        {
            std::lock_guard<std::mutex> l(gM);
            gDone.emplace_back(url, s);
            gBusy--;
        }
        gIdleCv.notify_all();
    }
}

}

void init(SDL_Renderer* r, Fetch fetch, const std::string& assetDir) {
    gR = r;
    gFetch = std::move(fetch);
    gAssets = assetDir;
    gStop = false;
    for (int i = 0; i < 2; i++) gThreads.emplace_back(work);
}

void shutdown() {
    {
        std::lock_guard<std::mutex> l(gM);
        gStop = true;
    }
    gCv.notify_all();
    for (auto& t : gThreads) t.join();
    gThreads.clear();
    for (auto& [k, s] : gSlots) if (s.tex) SDL_DestroyTexture(s.tex);
    for (auto& [k, t] : gIcons) if (t) SDL_DestroyTexture(t);
    for (auto& [k, s] : gDone) if (s) SDL_DestroySurface(s);
    gSlots.clear();
    gIcons.clear();
    gDone.clear();
}

void setRenderer(SDL_Renderer* r) {
    for (auto& [k, s] : gSlots) if (s.tex) SDL_DestroyTexture(s.tex);
    for (auto& [k, t] : gIcons) if (t) SDL_DestroyTexture(t);
    gSlots.clear();
    gIcons.clear();
    gR = r;
}

const std::string& assetDir() { return gAssets; }

SDL_Texture* get(const std::string& url) {
    if (url.empty()) return nullptr;
    auto it = gSlots.find(url);
    if (it != gSlots.end()) return it->second.tex;
    gSlots[url] = Slot{};
    {
        std::lock_guard<std::mutex> l(gM);
        gQueue.push_back(url);
    }
    gCv.notify_one();
    return nullptr;
}

bool failed(const std::string& url) {
    auto it = gSlots.find(url);
    return it != gSlots.end() && it->second.state == State::Failed;
}

void pump() {
    std::vector<std::pair<std::string, SDL_Surface*>> done;
    {
        std::lock_guard<std::mutex> l(gM);
        done.swap(gDone);
    }
    for (auto& [url, s] : done) {
        Slot& slot = gSlots[url];
        if (!s) {
            slot.state = State::Failed;
            continue;
        }
        if (slot.tex) SDL_DestroyTexture(slot.tex);
        slot.tex = SDL_CreateTextureFromSurface(gR, s);
        SDL_DestroySurface(s);
        if (slot.tex) {
            SDL_SetTextureScaleMode(slot.tex, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(slot.tex, SDL_BLENDMODE_BLEND);
        }
        slot.state = slot.tex ? State::Ready : State::Failed;
    }
}

void waitIdle() {
    std::unique_lock<std::mutex> l(gM);
    gIdleCv.wait(l, [] { return gQueue.empty() && gBusy == 0; });
    l.unlock();
    pump();
}

SDL_Texture* icon(const std::string& name, float px) {
    int sz = std::max(4, (int)std::lround(px));
    std::string key = name + "@" + std::to_string(sz);
    auto it = gIcons.find(key);
    if (it != gIcons.end()) return it->second;
    SDL_Texture* t = nullptr;
    std::string path = gAssets + "/icons/" + name + ".svg";
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (io) {
        SDL_Surface* s = IMG_LoadSizedSVG_IO(io, sz, sz);
        SDL_CloseIO(io);
        if (s) {
            t = SDL_CreateTextureFromSurface(gR, s);
            SDL_DestroySurface(s);
            if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        }
    }
    if (!t) SDL_Log("icon %s: %s", path.c_str(), SDL_GetError());
    gIcons[key] = t;
    return t;
}

SDL_Texture* asset(const std::string& file) {
    std::string key = "asset:" + file;
    auto it = gIcons.find(key);
    if (it != gIcons.end()) return it->second;
    std::string path = gAssets + "/" + file;
    SDL_Texture* t = IMG_LoadTexture(gR, path.c_str());
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    else SDL_Log("asset %s: %s", path.c_str(), SDL_GetError());
    gIcons[key] = t;
    return t;
}

}
