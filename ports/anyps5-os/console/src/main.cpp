#include "core/App.h"
#include <SDL3_image/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool size(const char* s, int& w, int& h) { return std::sscanf(s, "%dx%d", &w, &h) == 2; }

int main(int argc, char** argv) {
    ui::Options o;
    std::string embedded;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--offscreen") {
            o.offscreen = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') size(val(), o.w, o.h);
        } else if (a == "--windowed") {
            o.windowed = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') size(val(), o.w, o.h);
        } else if (a == "--host-file") o.hostFile = val();
        else if (a == "--host") o.hostUrl = val();
        else if (a == "--keys") o.keys = val();
        else if (a == "--shot") o.shot = val();
        else if (a == "--start") o.start = val();
        else if (a == "--boot-ms") o.bootMs = std::atoi(val());
        else if (a == "--key-ms") o.keyMs = std::atoi(val());
        else if (a == "--settle-ms") o.settleMs = std::atoi(val());
        else if (a == "--pads") o.pads = true;
        else if (a == "--selftest") return ui::inputSelfTest();
        else if (a == "--embedded-test") embedded = val();
        else if (a == "--gpu") o.gpu = true;
        else {
            std::printf("usage: anyps5-console [--windowed WxH] [--offscreen WxH] [--host URL | --host-file fixture.json]\n"
                        "       [--keys a,b,wait:800,text:abc] [--shot out.png] [--start screen] [--boot-ms N] [--key-ms N] [--settle-ms N] [--pads] [--selftest]\n");
            return a == "--help" ? 0 : 2;
        }
    }
    ui::App app;
    if (embedded.empty()) return app.run(o);
    o.offscreen = !o.hostFile.empty();
    SDL_Surface* surface = SDL_CreateSurface(o.w, o.h, SDL_PIXELFORMAT_RGBA32);
    if (!surface || !app.beginEmbedded(o, surface, "")) return 1;
    std::vector<ui::Action> none;
    for (int f = 0; f < 240; f++) app.stepEmbedded(1.0 / 60.0, none);
    bool saved = IMG_SavePNG(surface, embedded.c_str());
    app.endEmbedded();
    std::printf("embedded %dx%d -> %s %s\n", o.w, o.h, embedded.c_str(), saved ? "saved" : SDL_GetError());
    return saved ? 0 : 1;
}
