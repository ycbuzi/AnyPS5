#pragma once
#include "core/Draw.h"
#include "core/Text.h"
#include <functional>
#include <string>
#include <vector>

namespace ui::osk {

std::u32string decode(const std::string& s);
std::string encode(const std::u32string& s);
std::string encode(char32_t c);
std::string lower(const std::string& s);
std::string upper(const std::string& s);
std::string fold(const std::string& s);

struct Key {
    std::string k, label, badge;
    int r = 0, c = 0, w = 1;
};

struct Field {
    std::u32string text;
    int cur = 0;
    void set(const std::string& s);
    std::string str() const { return encode(text); }
};

class Keyboard {
public:
    std::string page = "abc";
    bool caps = false;
    void reset() { page = "abc"; caps = false; }
    std::vector<Key> layout() const;
    int keyCount() const { return (int)layout().size(); }
    std::string keyAt(int i) const;
    int move(int i, const std::string& dir) const;
    void edit(Field& f, const std::string& k, int max, bool capFirst);
    float height() const;
    void draw(Draw& d, float x, float y, int sel, bool focused) const;

private:
    std::string keyLabel(const Key& k) const;
};

void caretText(Draw& d, const std::string& shown, int cur, float x, float cy, const TextStyle& st, float caretH);
float caretAlpha();
void pageBackground(Draw& d);

struct Opts {
    int max = 24;
    bool capFirst = true;
    bool password = false;
};

void open(const std::string& title, const std::string& text, std::function<void(const std::string&)> done, Opts o = {});

}
