#include "core/App.h"
#include "core/Input.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace ui {

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

const char* sourceId(Source s) {
    switch (s) {
    case Source::Keyboard: return "keyboard";
    case Source::Mouse: return "mouse";
    case Source::Touch: return "touch";
    case Source::PlayStation: return "playstation";
    case Source::Xbox: return "xbox";
    case Source::Nintendo: return "nintendo";
    case Source::Generic: return "generic";
    case Source::Quest: return "quest";
    }
    return "keyboard";
}

bool sourceFromId(const std::string& id, Source& out) {
    for (Source s : {Source::Keyboard, Source::Mouse, Source::Touch, Source::PlayStation, Source::Xbox, Source::Nintendo, Source::Generic, Source::Quest})
        if (id == sourceId(s)) {
            out = s;
            return true;
        }
    return false;
}

const char* sourceLabel(Source s) {
    switch (s) {
    case Source::Keyboard: return "Keyboard";
    case Source::Mouse: return "Mouse";
    case Source::Touch: return "Touchscreen";
    case Source::PlayStation: return "PlayStation controller";
    case Source::Xbox: return "Xbox controller";
    case Source::Nintendo: return "Nintendo controller";
    case Source::Generic: return "Controller";
    case Source::Quest: return "Quest Touch controllers";
    }
    return "Keyboard";
}

Source padFamily(int sdlGamepadType, int vendor, const std::string& name) {
    switch (sdlGamepadType) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return Source::PlayStation;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return Source::Xbox;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return Source::Nintendo;
    default: break;
    }
    if (vendor == 0x054c) return Source::PlayStation;
    if (vendor == 0x045e) return Source::Xbox;
    if (vendor == 0x057e) return Source::Nintendo;
    std::string n = lower(name);
    if (has(n, "xbox") || has(n, "x-box") || has(n, "xinput")) return Source::Xbox;
    if (has(n, "dualsense") || has(n, "dualshock") || has(n, "playstation")) return Source::PlayStation;
    if (has(n, "nintendo") || has(n, "joy-con") || has(n, "pro controller")) return Source::Nintendo;
    return Source::Generic;
}

std::string fallbackPadMapping(int buttons, int axes, int hats) {
    if (buttons < 4 || axes < 2) return "";
    std::string m = "a:b2,b:b1,x:b3,y:b0,";
    if (buttons >= 6) m += "leftshoulder:b4,rightshoulder:b5,";
    if (buttons >= 8) m += "lefttrigger:b6,righttrigger:b7,";
    if (buttons >= 10) m += "back:b8,start:b9,";
    if (buttons >= 12) m += "leftstick:b10,rightstick:b11,";
    if (buttons >= 13) m += "guide:b12,";
    m += "leftx:a0,lefty:a1,";
    if (axes >= 4) m += "rightx:a2,righty:a3,";
    if (hats >= 1) m += "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,";
    else if (axes >= 6) m += "dpleft:-a4,dpright:+a4,dpup:-a5,dpdown:+a5,";
    return m;
}

static std::string mapFile() {
    const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!home || !*home) return "";
    return (std::filesystem::path(home) / ".local" / "share" / "anyps5" / "padmaps.txt").string();
}

static void saveMapping(const std::string& guid, const std::string& line) {
    std::string path = mapFile();
    if (path.empty()) return;
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string l;
        while (std::getline(in, l))
            if (!l.empty() && l.rfind(guid, 0) != 0) lines.push_back(l);
    }
    lines.push_back(line);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    for (auto& l : lines) out << l << "\n";
    if (!out) SDL_Log("pad: cannot write %s", path.c_str());
}

void Input::joystickAdded(SDL_JoystickID id) {
    if (SDL_IsGamepad(id) || fallback_.count(id)) return;
    const char* nm = SDL_GetJoystickNameForID(id);
    std::string name = nm && *nm ? nm : "Controller";
    std::string low = lower(name);
    if (has(low, "motion sensors") || has(low, "touchpad") || has(low, "keyboard") || has(low, "mouse")) return;
    SDL_Joystick* j = SDL_OpenJoystick(id);
    if (!j) return;
    int nb = SDL_GetNumJoystickButtons(j), na = SDL_GetNumJoystickAxes(j), nh = SDL_GetNumJoystickHats(j);
    SDL_CloseJoystick(j);
    std::string body = fallbackPadMapping(nb, na, nh);
    if (body.empty()) {
        SDL_Log("pad: '%s' is not a gamepad (%d buttons, %d axes), ignored", name.c_str(), nb, na);
        return;
    }
    char guid[33];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
    std::replace(name.begin(), name.end(), ',', ' ');
    std::string line = std::string(guid) + "," + name + "," + body + "platform:" + SDL_GetPlatform() + ",";
    if (SDL_AddGamepadMapping(line.c_str()) < 0) {
        SDL_Log("pad: mapping for '%s' refused: %s", name.c_str(), SDL_GetError());
        return;
    }
    fallback_[id] = true;
    SDL_Log("pad: '%s' has no known layout, using positional defaults (%d buttons, %d axes, %d hats)", name.c_str(), nb, na, nh);
    if (!app().deterministic) saveMapping(guid, line);
    if (SDL_IsGamepad(id)) openPad(id);
}

static std::string connectionOf(SDL_Gamepad* g, SDL_JoystickID id) {
    switch (SDL_GetGamepadConnectionState(g)) {
    case SDL_JOYSTICK_CONNECTION_WIRED: return "usb";
    case SDL_JOYSTICK_CONNECTION_WIRELESS: return "bluetooth";
    default: break;
    }
    SDL_GUID guid = SDL_GetGamepadGUIDForID(id);
    int bus = guid.data[0] | (guid.data[1] << 8);
    return bus == 5 ? "bluetooth" : bus == 3 ? "usb" : "";
}

static int batteryOf(SDL_Gamepad* g) {
    int pct = -1;
    SDL_PowerState st = SDL_GetGamepadPowerInfo(g, &pct);
    return st == SDL_POWERSTATE_ERROR || st == SDL_POWERSTATE_UNKNOWN ? -1 : pct;
}

void Input::openPad(SDL_JoystickID id) {
    if (pads_.count(id)) return;
    const char* listed = SDL_GetJoystickNameForID(id);
    const std::string listedLow = lower(listed ? listed : "");
    if (has(listedLow, "motion sensors") || has(listedLow, "touchpad")) return;
    SDL_Gamepad* g = SDL_OpenGamepad(id);
    if (!g) {
        SDL_Log("pad: cannot open %u: %s", (unsigned)id, SDL_GetError());
        return;
    }
    Pad p;
    p.g = g;
    const char* jn = SDL_GetJoystickName(SDL_GetGamepadJoystick(g));
    const char* gn = SDL_GetGamepadName(g);
    p.info.id = (unsigned)id;
    p.info.name = jn && *jn ? jn : gn && *gn ? gn : "Controller";
    p.info.vendor = SDL_GetGamepadVendor(g);
    p.info.product = SDL_GetGamepadProduct(g);
    p.info.family = padFamily((int)SDL_GetGamepadType(g), p.info.vendor, p.info.name + " " + (gn ? gn : ""));
    p.info.guide = SDL_GamepadHasButton(g, SDL_GAMEPAD_BUTTON_GUIDE);
    p.info.rumble = SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    p.info.fallbackMap = fallback_.count(id) > 0;
    p.info.connection = connectionOf(g, id);
    p.info.battery = batteryOf(g);
    SDL_Log("pad: '%s' %04x:%04x %s%s%s, %s, %s", p.info.name.c_str(), p.info.vendor, p.info.product, sourceId(p.info.family),
            p.info.connection.empty() ? "" : " ", p.info.connection.c_str(),
            p.info.guide ? "home button" : "no home button (Start+Select)", p.info.rumble ? "rumble" : "no rumble");
    if (char* mapping = SDL_GetGamepadMapping(g)) {
        SDL_Log("pad: layout %s", mapping);
        SDL_free(mapping);
    }
    pads_[id] = p;
    padPointer_.clear();
}

std::vector<PadInfo> Input::padList() const {
    std::vector<PadInfo> out;
    for (auto& kv : pads_) {
        PadInfo i = kv.second.info;
        if (kv.second.g) i.battery = batteryOf(kv.second.g);
        out.push_back(i);
    }
    return out;
}

bool Input::rumble(int vendor, int product, float strength, int ms) {
    bool any = false;
    Uint16 level = (Uint16)std::clamp(strength * 65535.f, 0.f, 65535.f);
    for (auto& kv : pads_) {
        const Pad& p = kv.second;
        if (!p.g || (vendor && (p.info.vendor != vendor || p.info.product != product))) continue;
        if (SDL_RumbleGamepad(p.g, level, level, (Uint32)ms)) any = true;
    }
    return any;
}

Source Input::padSource() const {
    if (isPad(source)) return source;
    auto it = pads_.find(lastPadId_);
    if (it != pads_.end()) return it->second.info.family;
    if (!pads_.empty()) return pads_.begin()->second.info.family;
    return isPad(buttonSource) ? buttonSource : Source::PlayStation;
}

bool Input::padGuide() const {
    auto it = pads_.find(lastPadId_);
    if (it != pads_.end()) return it->second.info.guide;
    if (!pads_.empty()) return pads_.begin()->second.info.guide;
    return true;
}

void Input::addTestPad(SDL_JoystickID id, Source family, bool guide) {
    Pad p;
    p.info.id = (unsigned)id;
    p.info.family = family;
    p.info.guide = guide;
    p.info.name = sourceLabel(family);
    pads_[id] = p;
    lastPadId_ = id;
}

bool Input::fromPadPointer(bool touch, Uint64 id) {
    if (pads_.empty()) return false;
    Uint64 key = id * 2 + (touch ? 1 : 0);
    auto it = padPointer_.find(key);
    if (it != padPointer_.end()) return it->second;
    const char* nm = !SDL_WasInit(SDL_INIT_VIDEO) ? nullptr : touch ? SDL_GetTouchDeviceName((SDL_TouchID)id) : SDL_GetMouseNameForID((SDL_MouseID)id);
    std::string name = lower(nm ? nm : "");
    bool mine = has(name, "controller touchpad");
    if (!mine && has(name, "touchpad"))
        for (auto& kv : pads_) {
            std::string pad = lower(kv.second.info.name);
            if (!pad.empty() && name.rfind(pad, 0) == 0) mine = true;
        }
    padPointer_[key] = mine;
    return mine;
}

}
