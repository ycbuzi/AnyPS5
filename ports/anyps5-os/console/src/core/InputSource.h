#pragma once
#include <string>

namespace ui {

enum class Source { Keyboard, Mouse, Touch, PlayStation, Xbox, Nintendo, Generic, Quest };

inline bool isPad(Source s) { return s == Source::PlayStation || s == Source::Xbox || s == Source::Nintendo || s == Source::Generic || s == Source::Quest; }
inline bool isPointer(Source s) { return s == Source::Mouse || s == Source::Touch; }

const char* sourceId(Source s);
bool sourceFromId(const std::string& id, Source& out);
const char* sourceLabel(Source s);
Source padFamily(int sdlGamepadType, int vendor, const std::string& name);
std::string fallbackPadMapping(int buttons, int axes, int hats);

struct PadInfo {
    unsigned id = 0;
    std::string name;
    Source family = Source::Generic;
    int vendor = 0, product = 0;
    bool guide = true, rumble = false, fallbackMap = false;
    std::string connection;
    int battery = -1;
};

}
