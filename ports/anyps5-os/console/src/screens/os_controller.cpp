#include "screens/settings.h"
#include <utility>

namespace ui::sett {

namespace {

const char* AREA = "controller";

using Options = std::vector<std::pair<std::string, std::string>>;

const Options LEVELS = {{"off", "Off"}, {"weak", "Weak"}, {"medium", "Medium"}, {"strong", "Strong"}};
const Options BRIGHT = {{"bright", "Bright"}, {"medium", "Medium"}, {"dim", "Dim"}};
const Options COLORS = {{"blue", "Blue"}, {"red", "Red"}, {"green", "Green"}, {"pink", "Pink"}, {"purple", "Purple"},
    {"orange", "Orange"}, {"white", "White"}, {"off", "Off"}};
const Options PLAYER = {{"auto", "Automatic"}, {"off", "Off"}};

struct PadState {
    bool loaded = false, unsupported = false, busy = false;
    std::string error, uid, preset = "default";
    bool hostOthers = false;
    json controllers = json::array(), settings = json::object(), presets = json::array(), buttons = json::array(), others = json::array();
};

PadState C;

std::string uid() { return app().meUid(); }

std::string label(const Options& o, const std::string& id) {
    for (auto& p : o)
        if (p.first == id) return p.second;
    return id;
}

std::string buttonName(const std::string& id) {
    for (auto& b : C.buttons)
        if (jstr(b, "id") == id) return jstr(b, "name");
    return id;
}

std::string setting(const char* k) { return jstr(C.settings, k); }

void take(const json& d) {
    if (d.contains("settings") && d["settings"].is_object()) C.settings = d["settings"];
    if (d.contains("preset") && d["preset"].is_string()) C.preset = d["preset"];
    if (d.contains("errors") && d["errors"].is_array() && !d["errors"].empty() && d["errors"][0].is_string())
        toast(d["errors"][0].get<std::string>());
}

void refresh() {
    C.uid = uid();
    sys(AREA, "status", json{{"uid", C.uid}}, [](const json& r) {
        C.loaded = true;
        if (!jbool(r, "ok")) {
            C.unsupported = jbool(r, "unsupported");
            C.error = C.unsupported ? "" : failText(r, "Could not read controllers");
            return;
        }
        C.unsupported = false;
        C.error.clear();
        json d = r.contains("data") && r["data"].is_object() ? r["data"] : json::object();
        C.controllers = d.contains("controllers") && d["controllers"].is_array() ? d["controllers"] : json::array();
        C.presets = d.contains("presets") && d["presets"].is_array() ? d["presets"] : json::array();
        C.buttons = d.contains("buttons") && d["buttons"].is_array() ? d["buttons"] : json::array();
        C.hostOthers = d.contains("others") && d["others"].is_array();
        C.others = C.hostOthers ? d["others"] : json::array();
        take(d);
    });
}

void call(const std::string& action, json args, std::function<void(const json&)> done = nullptr) {
    if (C.busy) return;
    C.busy = true;
    args["uid"] = C.uid;
    sys(AREA, action, args, [done](const json& r) {
        C.busy = false;
        if (!jbool(r, "ok")) {
            toast(failText(r));
            return;
        }
        json d = r.contains("data") && r["data"].is_object() ? r["data"] : json::object();
        take(d);
        if (done) done(d);
    });
}

void set(const std::string& key, const json& value) { call("set", json{{"key", key}, {"value", value}}); }

void choose(const std::string& key, const Options& o) {
    std::string cur = setting(key.c_str());
    std::vector<MenuItem> items;
    for (auto& p : o) {
        std::string id = p.first;
        items.push_back({p.second, [key, id] { set(key, id); }, id == cur});
    }
    menu(items, true);
}

void test(const std::string& what) {
    if (C.controllers.empty()) {
        toast("Connect a DualSense wireless controller first.");
        return;
    }
    call("test", json{{"what", what}}, [what](const json&) {
        if (what == "rumble") toast("Your controller should vibrate now.");
        else if (what == "trigger") toast("Press L2 and R2 now: the triggers push back for 5 seconds.");
        else toast("The light bar shows your colour now.");
    });
}

std::string padText(const json& p) {
    std::string s = jstr(p, "connection") == "usb" ? "USB" : "Bluetooth";
    if (p.contains("battery") && p["battery"].is_object() && p["battery"].contains("level") && p["battery"]["level"].is_number_integer())
        s += ", Battery " + std::to_string(p["battery"]["level"].get<int>()) + "%";
    return s;
}

void padMenu() {
    menu({{"Test Vibration", [] { test("rumble"); }}, {"Test Trigger Effect", [] { test("trigger"); }},
             {"Test Light Bar", [] { test("light"); }}},
        true);
}

bool loading(Rows& R, const char* what) {
    if (C.uid != uid()) refresh();
    if (!C.loaded) {
        R.info(what, "Checking...");
        return true;
    }
    if (C.unsupported) {
        R.info(what, AV);
        return true;
    }
    if (!C.error.empty()) {
        R.info(what, C.error);
        R.push("Check Again", "", refresh);
        return true;
    }
    return false;
}

bool isDualSense(int vendor, int product) { return vendor == 0x054c && (product == 0x0ce6 || product == 0x0df2); }

const char* familyName(Source s) {
    return s == Source::PlayStation ? "PlayStation" : s == Source::Xbox ? "Xbox" : s == Source::Nintendo ? "Nintendo" : "Controller";
}

std::vector<json> otherPads() {
    std::vector<json> out;
    auto sdl = app().input.padList();
    auto extra = [&sdl](json p) {
        int v = p.value("vendor", 0), pr = p.value("product", 0);
        p["rumble"] = nullptr;
        for (auto& i : sdl)
            if (i.vendor == v && i.product == pr) {
                p["rumble"] = i.rumble;
                p["assumed"] = i.fallbackMap;
                p["home"] = i.guide;
            }
        return p;
    };
    if (C.hostOthers) {
        for (auto& p : C.others)
            if (p.is_object()) out.push_back(extra(p));
        return out;
    }
    for (auto& i : sdl) {
        if (isDualSense(i.vendor, i.product)) continue;
        json p{{"name", i.name}, {"type", familyName(i.family)}, {"vendor", i.vendor}, {"product", i.product}, {"connection", i.connection}};
        if (i.battery >= 0) p["battery"] = json{{"level", i.battery}};
        out.push_back(extra(p));
    }
    return out;
}

std::string otherText(const json& p) {
    std::string s = jstr(p, "type", "Controller");
    std::string c = jstr(p, "connection");
    if (c == "usb") s += ", USB";
    else if (c == "bluetooth") s += ", Bluetooth";
    if (p.contains("battery") && p["battery"].is_object() && p["battery"].contains("level") && p["battery"]["level"].is_number_integer())
        s += ", Battery " + std::to_string(p["battery"]["level"].get<int>()) + "%";
    return s;
}

void otherRumble(int vendor, int product) {
    std::string v = setting("vibration");
    if (v == "off") {
        toast("Vibration is off. Choose an intensity first.");
        return;
    }
    float level = v == "weak" ? .33f : v == "medium" ? .66f : 1.f;
    if (app().input.rumble(vendor, product, level, 600)) toast("Your controller should vibrate now.");
    else toast("AnyPS5 OS cannot make this controller vibrate.");
}

void otherMenu(const json& p) {
    int vendor = p.value("vendor", 0), product = p.value("product", 0);
    std::vector<MenuItem> items;
    items.push_back({"Test Vibration", [vendor, product] { otherRumble(vendor, product); }});
    std::string home = !p.contains("home") ? "" : p.value("home", true) ? "It has a Home button for the control center."
                                                                           : "No Home button: press Start and Select together for the control center.";
    std::string layout = p.value("assumed", false) ? " AnyPS5 OS does not know this model, so it uses the standard button positions." : "";
    items.push_back({"About This Controller", [home, layout] {
        toast("Buttons, sticks and vibration can be used. Trigger effects, light bar and button assignments are for DualSense only. " + home + layout);
    }});
    menu(items, true);
}

void controllersRows(Rows& R) {
    if (loading(R, "Controllers")) return;
    auto others = otherPads();
    if (C.controllers.empty() && others.empty()) {
        R.info("No Controller Connected", "");
        R.note("Connect a controller with a USB cable, or register it in Bluetooth Accessories. Then choose Refresh List.");
    }
    bool locked = false;
    for (auto& p : C.controllers) {
        R.push(jstr(p, "name", "DualSense Wireless Controller"), padText(p), padMenu).valIcon = "bt-gamepad";
        locked = locked || !jbool(p, "writable", true);
    }
    for (auto& p : others) R.push(jstr(p, "name", "Controller"), otherText(p), [p] { otherMenu(p); }).valIcon = "bt-gamepad";
    R.push("Refresh List", "", refresh);
    if (!others.empty())
        R.note("Xbox, Nintendo and other controllers work in AnyPS5 OS and in games: buttons, sticks, and vibration where the controller has it. "
               "Trigger effects, the light bar and custom button assignments below are for DualSense controllers.");
    if (locked) R.note("AnyPS5 OS cannot send settings to this controller yet. Disconnect it and connect it again so the controller rule gives you access.");
}

void feelRows(Rows& R) {
    if (loading(R, "Vibration")) return;
    R.push("Vibration Intensity", label(LEVELS, setting("vibration")), [] { choose("vibration", LEVELS); });
    R.push("Trigger Effect Intensity", label(LEVELS, setting("trigger")), [] { choose("trigger", LEVELS); });
    R.sh("Try It");
    R.push("Test Vibration", "", [] { test("rumble"); });
    R.push("Test Trigger Effect", "", [] { test("trigger"); });
    R.note("Games started from AnyPS5 OS use these intensities. Off for trigger effects lowers the trigger force to the minimum the controller allows.");
}

void lightRows(Rows& R) {
    if (loading(R, "Light Bar")) return;
    R.push("Brightness of Controller Indicators", label(BRIGHT, setting("brightness")), [] { choose("brightness", BRIGHT); });
    R.push("Light Bar Colour", label(COLORS, setting("color")), [] { choose("color", COLORS); });
    R.push("Player Indicator", label(PLAYER, setting("playerLed")), [] { choose("playerLed", PLAYER); });
    R.push("Test Light Bar", "", [] { test("light"); });
    R.note("A game can change the light bar colour while you play, as on a PS5.");
}

void presetMenu() {
    std::vector<MenuItem> items;
    for (auto& p : C.presets) {
        std::string id = jstr(p, "id");
        items.push_back({jstr(p, "name"), [id] { call("preset", json{{"preset", id}}); }, id == C.preset});
    }
    menu(items, true);
}

void assignMenu(const std::string& button) {
    const json& remap = C.settings.contains("remap") && C.settings["remap"].is_object() ? C.settings["remap"] : json::object();
    std::string cur = jstr(remap, button.c_str(), button);
    std::vector<MenuItem> items;
    for (auto& b : C.buttons) {
        std::string id = jstr(b, "id");
        items.push_back({jstr(b, "name"), [button, id] { call("assign", json{{"button", button}, {"function", id}}); }, id == cur});
    }
    menu(items, true);
}

void buttonRows(Rows& R) {
    if (loading(R, "Button Assignments")) return;
    std::string presetName = C.preset == "custom" ? "Custom" : C.preset;
    for (auto& p : C.presets)
        if (jstr(p, "id") == C.preset) presetName = jstr(p, "name");
    R.push("Preset", presetName, presetMenu);
    R.tog("Swap Left and Right Sticks", [] { return jbool(C.settings, "swapSticks"); }, [](bool v) { set("swapSticks", v); });
    R.sh("Buttons");
    const json& remap = C.settings.contains("remap") && C.settings["remap"].is_object() ? C.settings["remap"] : json::object();
    for (auto& b : C.buttons) {
        std::string id = jstr(b, "id");
        std::string to = jstr(remap, id.c_str());
        R.push(jstr(b, "name"), to.empty() ? "Default" : buttonName(to), [id] { assignMenu(id); });
    }
    R.push("Reset Button Assignments", "", [] {
        menu({{"Reset Button Assignments", [] { call("resetButtons", json::object(), [](const json&) { toast("Buttons are back to their defaults."); }); }},
                 {"Cancel", [] {}}},
            true);
    });
    R.note("Choosing a function for a button swaps it with the button that had that function. Games started from AnyPS5 OS use these assignments; AnyPS5 OS menus keep the standard buttons.");
}

OsArea make() {
    OsArea a;
    a.id = AREA;
    a.cat = "pad";
    a.title = "Controllers";
    a.order = 40;
    a.sections = [] {
        return std::vector<Section>{{"Connected Controllers", "", controllersRows}, {"Vibration and Trigger Effect", "", feelRows},
            {"Light Bar and Indicators", "", lightRows}, {"Custom Button Assignments", "", buttonRows}};
    };
    a.refresh = refresh;
    return a;
}

}

ANYPS5_OS_AREA(controller, make);

}
