#include "widgets/osk.h"
#include <map>

namespace ui::osk {

std::u32string decode(const std::string& s) {
    std::u32string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        else if (c >= 0x80) { cp = 0xFFFD; }
        i++;
        for (int k = 0; k < extra; k++) {
            if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            i++;
        }
        out.push_back(cp);
    }
    return out;
}

std::string encode(char32_t c) {
    std::string o;
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    return o;
}

std::string encode(const std::u32string& s) {
    std::string o;
    for (char32_t c : s) o += encode(c);
    return o;
}

static bool pairUpperEven(char32_t c) {
    return (c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177);
}

static bool pairUpperOdd(char32_t c) {
    return (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
}

static char32_t lowerCp(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;
    if (pairUpperEven(c) && c % 2 == 0) return c + 1;
    if (pairUpperOdd(c) && c % 2 == 1) return c + 1;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

static std::u32string upperCp(char32_t c) {
    if (c >= 'a' && c <= 'z') return {(char32_t)(c - 32)};
    if (c == 0xDF) return U"SS";
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return {(char32_t)(c - 32)};
    if (c == 0xFF) return {0x178};
    if (c == 0x131) return {U'I'};
    if (pairUpperEven(c) && c % 2 == 1) return {(char32_t)(c - 1)};
    if (pairUpperOdd(c) && c % 2 == 0) return {(char32_t)(c - 1)};
    if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return {(char32_t)(c - 32)};
    if (c >= 0x430 && c <= 0x44F) return {(char32_t)(c - 32)};
    if (c >= 0x450 && c <= 0x45F) return {(char32_t)(c - 80)};
    return {c};
}

std::string lower(const std::string& s) {
    std::u32string u = decode(s);
    for (auto& c : u) c = lowerCp(c);
    return encode(u);
}

std::string upper(const std::string& s) {
    std::u32string o;
    for (char32_t c : decode(s)) o += upperCp(c);
    return encode(o);
}

static const char* baseOf(char32_t c) {
    static const std::map<char32_t, const char*> m = {
        {0xE0, "a"}, {0xE1, "a"}, {0xE2, "a"}, {0xE3, "a"}, {0xE4, "a"}, {0xE5, "a"}, {0xE7, "c"}, {0xE8, "e"}, {0xE9, "e"},
        {0xEA, "e"}, {0xEB, "e"}, {0xEC, "i"}, {0xED, "i"}, {0xEE, "i"}, {0xEF, "i"}, {0xF1, "n"}, {0xF2, "o"}, {0xF3, "o"},
        {0xF4, "o"}, {0xF5, "o"}, {0xF6, "o"}, {0xF9, "u"}, {0xFA, "u"}, {0xFB, "u"}, {0xFC, "u"}, {0xFD, "y"}, {0xFF, "y"},
        {0xDF, "ss"}, {0xE6, "ae"}, {0x153, "oe"}, {0xF8, "o"}, {0x142, "l"}, {0x111, "d"}, {0xF0, "d"}, {0xFE, "th"}, {0x131, "i"},
        {0x101, "a"}, {0x103, "a"}, {0x105, "a"}, {0x107, "c"}, {0x109, "c"}, {0x10B, "c"}, {0x10D, "c"}, {0x10F, "d"},
        {0x113, "e"}, {0x115, "e"}, {0x117, "e"}, {0x119, "e"}, {0x11B, "e"}, {0x11D, "g"}, {0x11F, "g"}, {0x121, "g"},
        {0x123, "g"}, {0x125, "h"}, {0x129, "i"}, {0x12B, "i"}, {0x12D, "i"}, {0x12F, "i"}, {0x135, "j"}, {0x137, "k"},
        {0x13A, "l"}, {0x13C, "l"}, {0x13E, "l"}, {0x144, "n"}, {0x146, "n"}, {0x148, "n"}, {0x14D, "o"}, {0x14F, "o"},
        {0x151, "o"}, {0x155, "r"}, {0x157, "r"}, {0x159, "r"}, {0x15B, "s"}, {0x15D, "s"}, {0x15F, "s"}, {0x161, "s"},
        {0x163, "t"}, {0x165, "t"}, {0x169, "u"}, {0x16B, "u"}, {0x16D, "u"}, {0x16F, "u"}, {0x171, "u"}, {0x173, "u"},
        {0x175, "w"}, {0x177, "y"}, {0x17A, "z"}, {0x17C, "z"}, {0x17E, "z"}};
    auto it = m.find(c);
    return it == m.end() ? nullptr : it->second;
}

std::string fold(const std::string& s) {
    std::string o;
    for (char32_t c : decode(s)) {
        c = lowerCp(c);
        if (c >= 0x300 && c <= 0x36F) continue;
        if (const char* b = baseOf(c)) o += b;
        else o += encode(c);
    }
    return o;
}

void Field::set(const std::string& s) {
    text = decode(s);
    cur = (int)text.size();
}

}
