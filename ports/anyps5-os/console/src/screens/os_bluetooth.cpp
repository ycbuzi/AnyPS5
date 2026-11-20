#include "screens/settings.h"
#include <map>
#include <set>

namespace ui::sett {

namespace {

const char* AREA = "bluetooth";

struct BtState {
    bool loaded = false, unsupported = false, available = false, canPower = false, powerBusy = false;
    int powered = -1, canPair = -1;
    std::string error, adapter;
    json devices = json::array(), found = json::array();
    bool scanning = false, scanned = false, scanUnsupported = false;
    std::map<std::string, std::string> busy;
};

BtState B;

std::string devName(const json& d) {
    std::string n = jstr(d, "name");
    bool blank = n.find_first_not_of(" \t") == std::string::npos;
    return blank ? "Unknown Accessory (" + jstr(d, "address") + ")" : n;
}

std::string stateText(const json& d) {
    auto b = B.busy.find(jstr(d, "address"));
    if (b != B.busy.end()) return b->second;
    if (d.contains("connected") && d["connected"] == true) return "Connected";
    if (d.contains("connected") && d["connected"] == false) return "Not Connected";
    std::string s = jstr(d, "state");
    return s.empty() ? "Registered" : s;
}

std::string typeIcon(const json& d) {
    static const std::set<std::string> known = {"gamepad", "audio", "keyboard", "mouse", "phone", "computer"};
    std::string t = jstr(d, "type");
    return "bt-" + (known.count(t) ? t : std::string("other"));
}

void refresh() {
    sys(AREA, "status", {}, [](const json& r) {
        B.loaded = true;
        if (jbool(r, "ok")) {
            json d = r.contains("data") && r["data"].is_object() ? r["data"] : json::object();
            B.unsupported = false;
            B.error.clear();
            B.available = jbool(d, "available");
            B.powered = d.contains("powered") && d["powered"].is_boolean() ? (int)d["powered"].get<bool>() : -1;
            B.canPower = jbool(d, "canPower");
            B.canPair = jbool(d, "canPair") ? 1 : 0;
            B.adapter = jstr(d, "adapter");
            B.devices = d.contains("devices") && d["devices"].is_array() ? d["devices"] : json::array();
            std::set<std::string> paired;
            for (auto& x : B.devices)
                if (jbool(x, "paired")) paired.insert(jstr(x, "address"));
            json keep = json::array();
            for (auto& x : B.found)
                if (!paired.count(jstr(x, "address"))) keep.push_back(x);
            B.found = keep;
        } else {
            B.unsupported = jbool(r, "unsupported");
            B.error = B.unsupported ? "" : (jstr(r, "error").empty() ? "Could not read Bluetooth" : jstr(r, "error"));
        }
    });
}

void setPower(bool on) {
    if (B.powerBusy) return;
    B.powerBusy = true;
    sys(AREA, "power", json{{"on", on}}, [on](const json& r) {
        B.powerBusy = false;
        toast(jbool(r, "ok") ? (on ? "Bluetooth is on." : "Bluetooth is off.") : failText(r));
        refresh();
    });
}

void runAction(const json& d, const std::string& action, const std::string& busyText, const std::string& doneText) {
    std::string addr = jstr(d, "address"), name = devName(d);
    B.busy[addr] = busyText;
    sys(AREA, action, json{{"address", addr}}, [addr, name, doneText](const json& r) {
        B.busy.erase(addr);
        toast(jbool(r, "ok") ? doneText : name + ": " + failText(r));
        refresh();
    });
}

void deviceMenu(const json& d) {
    if (B.busy.count(jstr(d, "address"))) return;
    std::string name = devName(d);
    std::vector<MenuItem> items;
    if (d.contains("connected") && d["connected"] == true)
        items.push_back({"Disconnect", [d, name] { runAction(d, "disconnect", "Disconnecting...", name + " is disconnected."); }});
    else items.push_back({"Connect", [d, name] { runAction(d, "connect", "Connecting...", name + " is connected."); }});
    items.push_back({"Remove", [d, name] {
                         menu({{"Remove " + name, [d, name] { runAction(d, "remove", "Removing...", name + " is removed."); }}, {"Cancel", [] {}}}, true);
                     }});
    menu(items, true);
}

void scan() {
    if (B.scanning) return;
    if (B.canPair == 0 || B.scanUnsupported) {
        toast(AV);
        return;
    }
    if (B.powered == 0) {
        toast("Turn on Bluetooth first.");
        return;
    }
    B.scanning = true;
    B.scanned = false;
    B.found = json::array();
    sys(AREA, "scan", json{{"seconds", 10}}, [](const json& r) {
        B.scanning = false;
        B.scanned = true;
        if (jbool(r, "ok")) {
            B.found = r.contains("data") && r["data"].contains("devices") && r["data"]["devices"].is_array() ? r["data"]["devices"] : json::array();
            size_t n = B.found.size();
            toast(n ? std::to_string(n) + (n == 1 ? " accessory found." : " accessories found.") : "No accessories found.");
        } else {
            if (jbool(r, "unsupported")) B.scanUnsupported = true;
            toast(failText(r));
        }
    });
}

void pair(const json& d) {
    std::string addr = jstr(d, "address"), name = devName(d);
    if (B.busy.count(addr)) return;
    json args{{"address", addr}};
    B.busy[addr] = "Pairing...";
    sys(AREA, "pair", args, [=](const json& r) {
        if (!jbool(r, "ok")) {
            B.busy.erase(addr);
            toast(name + ": " + failText(r));
            return;
        }
        B.busy[addr] = "Registering...";
        sys(AREA, "trust", args, [=](const json& t) {
            B.busy[addr] = "Connecting...";
            sys(AREA, "connect", args, [=](const json& c) {
                B.busy.erase(addr);
                if (!jbool(t, "ok")) toast(name + " is paired, but AnyPS5 could not remember it: " + failText(t));
                else if (!jbool(c, "ok")) toast(name + " is paired, but could not connect: " + failText(c));
                else toast(name + " is registered and connected.");
                json keep = json::array();
                for (auto& x : B.found)
                    if (jstr(x, "address") != addr) keep.push_back(x);
                B.found = keep;
                refresh();
            });
        });
    });
}

void accessoriesRows(Rows& R) {
    if (!B.loaded) {
        R.info("Bluetooth", "Checking...");
        return;
    }
    if (B.unsupported) {
        R.info("Bluetooth", AV);
        return;
    }
    if (!B.error.empty()) {
        R.info("Bluetooth", B.error);
        R.push("Check Again", "", refresh);
        return;
    }
    if (!B.available) R.info("Bluetooth Adapter", "Not Found");
    else {
        if (B.canPower) R.tog("Bluetooth", [] { return B.powered == 1; }, setPower);
        else if (B.powered >= 0) R.info("Bluetooth", B.powered ? "On" : "Off");
        if (B.powerBusy) R.info("Bluetooth", "Changing...");
        if (!B.adapter.empty()) R.info("Bluetooth Adapter", B.adapter);
    }
    R.sh("Registered Accessories");
    if (B.devices.empty()) R.info("No Accessories Registered", "");
    for (auto& d : B.devices) {
        json dev = d;
        R.push(devName(d), stateText(d), [dev] { deviceMenu(dev); }).valIcon = typeIcon(d);
    }
    R.push("Refresh List", "", refresh);
    if (!B.canPower) R.note("On this PC, AnyPS5 lists the Bluetooth accessories Windows knows about. Connecting, disconnecting and removing them here is available in AnyPS5 OS; until then use Bluetooth & devices in Windows Settings.");
}

void addRows(Rows& R) {
    bool blocked = B.canPair == 0 || B.scanUnsupported;
    if (B.scanning) R.info("Searching for Accessories...", "About 10 seconds");
    else R.push(B.scanned ? "Search Again" : "Search for Accessories", blocked ? AV : (B.loaded ? "" : "Checking..."), scan);
    if (!blocked && B.scanned && !B.scanning) {
        R.sh("Accessories Found");
        if (B.found.empty()) R.info("No Accessories Found", "");
        for (auto& d : B.found) {
            json dev = d;
            auto b = B.busy.find(jstr(d, "address"));
            R.push(devName(d), b != B.busy.end() ? b->second : "Pair", [dev] { pair(dev); }).valIcon = typeIcon(d);
        }
    }
    R.sh("DualSense Wireless Controller");
    R.note("To put a DualSense in pairing mode, hold the Create button and the PS button at the same time until the light bar blinks. An Xbox controller: hold the small pair button on the top edge until the Xbox button flashes quickly. A Switch Pro Controller: hold the sync button on the top edge. Then choose Search for Accessories and select the controller when it appears.");
}

OsArea make() {
    OsArea a;
    a.id = AREA;
    a.cat = "pad";
    a.title = "Bluetooth Accessories";
    a.order = 50;
    a.sections = [] { return std::vector<Section>{{"Bluetooth Accessories", "", accessoriesRows}, {"Add Bluetooth Accessory", "", addRows}}; };
    a.refresh = refresh;
    return a;
}

}

ANYPS5_OS_AREA(bluetooth, make);

}
