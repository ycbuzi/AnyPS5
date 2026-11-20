#include "screens/settings.h"
#include "core/Glyphs.h"
#include <array>
#include <map>

namespace ui::sett {

static std::string sv(const std::string& k, const std::string& d) {
    json v = app().settings.a11yv(k, d);
    return v.is_string() ? v.get<std::string>() : d;
}

static void setA(const std::string& k, const json& v) {
    app().settings.setA11y(k, v);
    saveApply();
}

static std::function<void(bool)> setter(const std::string& k) {
    return [k](bool v) { setA(k, v); };
}

static std::function<bool()> getter(const std::string& k) {
    return [k] { return app().settings.a11y(k); };
}

static void cycle(Rows& R, const std::string& label, const std::string& key, const std::string& d,
    const std::vector<std::pair<std::string, std::string>>& opts) {
    std::string cur = sv(key, d);
    std::string name = opts[0].second;
    size_t at = 0;
    for (size_t i = 0; i < opts.size(); i++)
        if (opts[i].first == cur) {
            name = opts[i].second;
            at = i;
        }
    std::string next = opts[(at + 1) % opts.size()].first;
    R.push(label, name, [key, next] { setA(key, next); });
}

static const std::vector<std::pair<std::string, std::string>> CC = {
    {"off", "Off"}, {"protan", "Protanopia (red)"}, {"deutan", "Deuteranopia (green)"}, {"tritan", "Tritanopia (blue)"}, {"gray", "Grayscale"}};

static void displayRows(Rows& R) {
    R.tog("Zoom", getter("zoom"), setter("zoom"));
    R.tog("Invert Colors", getter("invert"), setter("invert"));
    cycle(R, "Color Correction", "cc", "off", CC);
    std::string cc = sv("cc", "off");
    if (cc != "off" && cc != "gray") {
        json s = app().settings.a11yv("ccStrength", 1);
        double v = s.is_number() ? s.get<double>() : 1;
        std::string name = v == .5 ? "Low" : v == .75 ? "Medium" : v == 1 ? "High" : "";
        R.push("Color Correction Intensity", name, [v] { setA("ccStrength", v == 1 ? .5 : v == .5 ? .75 : 1); });
    }
    cycle(R, "Text Size", "text", "default", {{"default", "Default"}, {"large", "Large"}, {"larger", "Larger"}});
    R.tog("Bold Text", getter("bold"), setter("bold"));
    R.tog("High Contrast", getter("contrast"), setter("contrast"));
    cycle(R, "Auto-Scroll Speed", "scroll", "normal", {{"normal", "Normal"}, {"fast", "Fast"}, {"slow", "Slow"}});
    R.tog("Reduce Motion", getter("motion"), setter("motion"));
    R.note("Zoom: with it on, press " + glyph::name("zoom", glyph::pad()) + " (or Z on the keyboard) to magnify around what is selected; press again to zoom out. Auto-Scroll Speed sets how fast a selected name that does not fit scrolls sideways.");
}

static void readerRows(Rows& R) {
    R.tog("Screen Reader", getter("reader"), setter("reader"));
    cycle(R, "Speed", "rate", "normal", {{"normal", "Normal"}, {"fast", "Fast"}, {"fastest", "Fastest"}, {"slow", "Slow"}});
    cycle(R, "Volume", "vol", "high", {{"high", "High"}, {"low", "Low"}, {"medium", "Medium"}});
    std::string voice = sv("voice", "");
    R.push("Voice", voice.empty() ? "No voices found" : voice, [] {});
    R.tog("Read Notifications Aloud", [] { json v = app().settings.a11yv("readNotes", true); return !v.is_boolean() || v.get<bool>(); }, setter("readNotes"));
    R.note("The screen reader reads what is selected, as you move. It uses the voices installed on this system, offline.");
}

static void padRows(Rows& R) {
    R.tog(glyph::swapLabel(glyph::pad()), getter("swap"), setter("swap"));
    cycle(R, "Press-and-Hold Speed", "hold", "normal", {{"normal", "Normal"}, {"fast", "Fast"}, {"slow", "Slow"}});
    R.note(glyph::swapLabel(glyph::pad()) + ": " + glyph::swapNote(glyph::pad()) + " Press-and-Hold Speed is how fast a held direction keeps moving.");
}

std::vector<Section> accessSections() {
    return {{"Display", "", displayRows}, {"Screen Reader", "", readerRows}, {"Controllers", "", padRows}};
}

}
