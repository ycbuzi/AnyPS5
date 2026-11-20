#include "core/Anim.h"
#include "screens/settings.h"
#include "core/Glyphs.h"
#include <algorithm>
#include <cstdio>

namespace ui::sett {

namespace {

const std::vector<std::pair<std::string, std::string>> RES = {{"720p", "720p"}, {"1080p", "1080p"}, {"native", "Native"}};
const std::vector<std::pair<std::string, std::string>> TYPES = {{"webm", "WebM"}, {"mp4", "MP4"}};
const std::vector<int> LENGTHS = {15, 30, 60, 300, 900};

struct RecState {
    json st;
    std::string busy;
    double replayAt = -1;
};

RecState Rc;

struct Opt {
    std::string resolution, format;
    int seconds;
    bool mic, keep;
};

std::string pick(const std::vector<std::pair<std::string, std::string>>& list, const json& v, const std::string& d) {
    if (!v.is_string()) return d;
    for (auto& x : list)
        if (x.first == v.get<std::string>()) return x.first;
    return d;
}

std::string nameOf(const std::vector<std::pair<std::string, std::string>>& list, const std::string& v) {
    for (auto& x : list)
        if (x.first == v) return x.second;
    return list[0].second;
}

std::string lenText(int s) {
    s = std::max(0, s);
    if (s < 60) return std::to_string(s) + (s == 1 ? " second" : " seconds");
    if (s % 60 == 0) {
        int m = s / 60;
        return std::to_string(m) + (m == 1 ? " minute" : " minutes");
    }
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

std::string lenLabel(int s) {
    std::string t = lenText(s);
    size_t sp = t.find(' ');
    if (sp != std::string::npos && sp + 1 < t.size()) t[sp + 1] = (char)std::toupper((unsigned char)t[sp + 1]);
    return t;
}

std::string clock(long long since) {
    long long s = std::max(0LL, (long long)app().wallNow() - since), h = s / 3600, m = (s / 60) % 60;
    char b[32];
    if (h) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", h, m, s % 60);
    else std::snprintf(b, sizeof b, "%lld:%02lld", m, s % 60);
    return b;
}

Opt opt() {
    const json& S = app().settings.S;
    json v = S.contains("video") && S["video"].is_object() ? S["video"] : json::object();
    Opt o;
    o.resolution = pick(RES, v.value("res", json()), "720p");
    o.format = pick(TYPES, v.value("type", json()), "webm");
    int rec = v.contains("recent") && v["recent"].is_number() ? v["recent"].get<int>() : 60;
    o.seconds = std::find(LENGTHS.begin(), LENGTHS.end(), rec) != LENGTHS.end() ? rec : 60;
    o.mic = jbool(v, "mic");
    o.keep = v.value("keep", true) != false;
    return o;
}

void setOpt(const std::string& k, const json& val) {
    json& S = app().settings.S;
    if (!S.contains("video") || !S["video"].is_object()) S["video"] = json::object();
    S["video"][k] = val;
    app().saveSettings();
    Rc.replayAt = now() + .8;
}

void refresh(std::function<void()> then = nullptr) {
    sys("recording", "status", {}, [then](const json& r) {
        if (jbool(r, "ok") && r.contains("data") && r["data"].is_object()) Rc.st = r["data"];
        else Rc.st = json{{"supported", false}, {"recording", false}, {"replay", false},
            {"reason", jbool(r, "unsupported") ? std::string(AV) : (jstr(r, "error").empty() ? "AnyPS5 did not answer" : jstr(r, "error"))},
            {"failed", !jbool(r, "unsupported")}};
        if (then) then();
    });
}

void applyReplay() {
    refresh([] {
        if (!jbool(Rc.st, "supported") || !jbool(Rc.st, "replaySupported")) return;
        Opt o = opt();
        if (!o.keep) {
            if (jbool(Rc.st, "replay"))
                sys("recording", "replayStop", {}, [](const json& r) {
                    if (!jbool(r, "ok")) toast(failText(r, "Could not stop recent gameplay"));
                    refresh();
                });
            return;
        }
        sys("recording", "replayStart", json{{"seconds", o.seconds}, {"resolution", o.resolution}, {"format", o.format}, {"mic", o.mic}}, [](const json& r) {
            if (!jbool(r, "ok")) toast(failText(r, "Could not keep recent gameplay"));
            refresh();
        });
    });
}

void tick(bool force) {
    if (Rc.replayAt < 0) return;
    if (!force && now() < Rc.replayAt) return;
    Rc.replayAt = -1;
    applyReplay();
}

void rows(Rows& R) {
    Opt o = opt();
    const json& st = Rc.st;
    bool have = st.is_object();
    bool sup = have && jbool(st, "supported");
    if (!have) R.info("Video Recording", "Checking...");
    else if (!sup) R.info("Video Recording", jbool(st, "failed") ? jstr(st, "reason") : AV);
    else {
        std::string v = Rc.busy == "start" ? "Starting..." : Rc.busy == "stop" ? "Saving..." : jbool(st, "recording") ? "Recording " + clock(st.value("startedAt", 0LL)) : "Ready";
        R.info("Video Recording", v);
        if (jbool(st, "replaySupported"))
            R.info("Recent Gameplay", Rc.busy == "save" ? "Saving..." : jbool(st, "replay") ? "Keeping the last " + lenText(st.value("replaySeconds", 0)) : "Off");
    }
    R.push("Video Clip Resolution", nameOf(RES, o.resolution), [o] {
        size_t i = 0;
        for (size_t k = 0; k < RES.size(); k++)
            if (RES[k].first == o.resolution) i = k;
        setOpt("res", RES[(i + 1) % RES.size()].first);
    });
    R.push("Video Clip File Type", nameOf(TYPES, o.format), [o] { setOpt("type", o.format == "webm" ? "mp4" : "webm"); });
    R.push("Length of Recent Gameplay Video Clip", lenLabel(o.seconds), [o] {
        size_t i = std::find(LENGTHS.begin(), LENGTHS.end(), o.seconds) - LENGTHS.begin();
        setOpt("recent", LENGTHS[(i + 1) % LENGTHS.size()]);
    });
    R.tog("Include Your Microphone Audio", [] { return opt().mic; }, [](bool v) { setOpt("mic", v); });
    if (sup && jbool(st, "replaySupported")) R.tog("Keep Recent Gameplay", [] { return opt().keep; }, [](bool v) { setOpt("keep", v); });
    if (sup && !jstr(st, "videoDir").empty()) R.info("Videos Folder", jstr(st, "videoDir")).secret = true;
    std::string reason = have ? jstr(st, "reason") : "";
    if (sup && !reason.empty()) R.note(reason);
    else if (have && !sup && !jbool(st, "failed")) R.note((!reason.empty() && reason != AV ? reason + " " : std::string()) + "These settings are kept for AnyPS5 OS.");
    if (sup) R.note(glyph::fill("Press {create} on the controller", glyph::pad()) + std::string(" to save recent gameplay or start a new recording. Recent gameplay is kept in memory; a longer length uses more memory. Changing a setting here restarts recent gameplay."));
}

OsArea make() {
    OsArea a;
    a.id = "recording";
    a.cat = "capture";
    a.title = "Video Clips";
    a.order = 60;
    a.sections = [] { return std::vector<Section>{{"Video Clips", "", rows}}; };
    a.refresh = [] { refresh(); };
    a.tick = tick;
    return a;
}

}

ANYPS5_OS_AREA(recording, make);

}
