#include "screens/settings.h"
#include <cstdio>

namespace ui::sett {

namespace {

struct TestItem {
    std::string text;
    bool bad = false;
};

struct NetTest {
    bool on = false, running = false;
    TestItem ip, net, down, up;
    std::string why;
};

struct NetState {
    json st;
    std::string stErr, view = "main", wifiErr, busy;
    bool stUnsup = false, loading = false, wifiUnsup = false, scanning = false;
    json wifi;
    NetTest test;
};

NetState N;

bool canControl() { return N.st.is_object() && jbool(N.st, "control"); }

void loadStatus() {
    if (N.loading) return;
    N.loading = true;
    sys("network", "status", {}, [](const json& r) {
        N.loading = false;
        if (jbool(r, "ok")) {
            N.st = r.contains("data") && r["data"].is_object() ? r["data"] : json::object();
            N.stErr.clear();
            N.stUnsup = false;
        } else {
            N.stUnsup = jbool(r, "unsupported");
            N.stErr = jstr(r, "error").empty() ? "Could not read the connection status" : jstr(r, "error");
        }
    });
}

void scan(bool rescan) {
    if (N.scanning) return;
    N.scanning = true;
    N.wifiErr.clear();
    sys("network", "wifi_list", json{{"rescan", rescan}}, [](const json& r) {
        N.scanning = false;
        if (jbool(r, "ok")) {
            N.wifi = r.contains("data") && r["data"].is_object() ? r["data"] : json{{"networks", json::array()}};
            N.wifiUnsup = false;
        } else {
            N.wifiUnsup = jbool(r, "unsupported");
            N.wifiErr = jstr(r, "error").empty() ? "Could not search for networks" : jstr(r, "error");
        }
    });
}

void act(const std::string& action, const json& args, const std::string& doing, std::function<std::string(const json&)> done) {
    if (!N.busy.empty()) {
        toast("Please wait. " + N.busy);
        return;
    }
    N.busy = doing;
    sys("network", action, args, [done](const json& r) {
        N.busy.clear();
        if (jbool(r, "ok")) {
            if (done) toast(done(r.contains("data") ? r["data"] : json::object()));
        } else toast(jbool(r, "unsupported") ? AV : (jstr(r, "error").empty() ? "Something went wrong" : jstr(r, "error")));
        loadStatus();
        if (N.view == "setup") scan(false);
    });
}

void connectTo(const json& n, const std::string& password, bool hidden) {
    std::string ssid = jstr(n, "ssid");
    json args{{"ssid", ssid}, {"hidden", hidden}};
    if (!jstr(n, "bssid").empty() && !hidden) args["bssid"] = jstr(n, "bssid");
    if (!password.empty()) args["password"] = password;
    act("connect", args, "Connecting to " + ssid + "...", [ssid](const json&) { return "Connected to " + ssid + "."; });
}

void askPassword(const json& n, bool hidden) {
    OskOpts o;
    o.max = 64;
    o.capFirst = false;
    o.password = true;
    osk("Password for " + jstr(n, "ssid"), "", [n, hidden](const std::string& pw) {
        if (!pw.empty()) connectTo(n, pw, hidden);
        else toast("No password entered.");
    }, o);
}

void pickNetwork(const json& n) {
    if (!canControl()) {
        toast(AV);
        return;
    }
    if (jbool(n, "inUse")) {
        std::string ssid = jstr(n, "ssid");
        menu({{"Disconnect", [ssid] { act("disconnect", json::object(), "Disconnecting...", [ssid](const json&) { return "Disconnected from " + ssid + "."; }); }},
            {"View Connection Status", [] {
                 N.view = "status";
                 resetRow();
             }}});
        return;
    }
    if (jbool(n, "secured")) askPassword(n, false);
    else connectTo(n, "", false);
}

void setUpManually() {
    if (!canControl()) {
        toast(AV);
        return;
    }
    OskOpts o;
    o.max = 32;
    o.capFirst = false;
    osk("Network Name (SSID)", "", [](const std::string& ssid) {
        if (ssid.empty()) return;
        json n{{"ssid", ssid}};
        menu({{"WPA/WPA2/WPA3 Personal", [n] { askPassword(n, true); }}, {"None (Open Network)", [n] { connectTo(n, "", true); }}});
    }, o);
}

std::string failWord(const json& r) { return jbool(r, "unsupported") ? AV : "Failed"; }

std::string mbps(const json& r) {
    double v = r.contains("data") ? r["data"].value("mbps", 0.0) : 0.0;
    char b[32];
    std::snprintf(b, sizeof b, "%.1f Mbps", v);
    return b;
}

void endTest(bool passed, const std::string& why) {
    N.test.running = false;
    if (!passed)
        for (TestItem* t : {&N.test.net, &N.test.down, &N.test.up})
            if (t->text.empty()) t->text = "Not Tested";
    N.test.why = passed ? "" : why;
    toast(passed ? "Internet connection test finished." : "The internet connection test failed.");
}

void runTest() {
    if (N.test.running) return;
    N.test = NetTest{};
    N.test.on = true;
    N.test.running = true;
    N.test.ip = {"Checking..."};
    sys("network", "test_ip", {}, [](const json& r) {
        if (!(jbool(r, "ok") && r.contains("data") && jbool(r["data"], "ok"))) {
            N.test.ip = {failWord(r), true};
            endTest(false, jbool(r, "ok") ? "No IP address was obtained." : jstr(r, "error"));
            return;
        }
        N.test.ip = {"Successful"};
        N.test.net = {"Checking..."};
        sys("network", "test_internet", {}, [](const json& r) {
            if (!(jbool(r, "ok") && r.contains("data") && jbool(r["data"], "ok"))) {
                N.test.net = {failWord(r), true};
                std::string e = jbool(r, "ok") ? jstr(r["data"], "error") : jstr(r, "error");
                endTest(false, e.empty() && jbool(r, "ok") ? "Could not reach the internet." : e);
                return;
            }
            N.test.net = {"Successful"};
            N.test.down = {"Checking..."};
            sys("network", "test_download", json{{"bytes", 10000000}}, [](const json& r) {
                N.test.down = jbool(r, "ok") && r.contains("data") ? TestItem{mbps(r)} : TestItem{failWord(r), true};
                N.test.up = {"Checking..."};
                sys("network", "test_upload", json{{"bytes", 2000000}}, [](const json& r) {
                    N.test.up = jbool(r, "ok") && r.contains("data") ? TestItem{mbps(r)} : TestItem{failWord(r), true};
                    endTest(true, "");
                });
            });
        });
    });
}

void testInfo(Rows& R, const std::string& label, const TestItem& t) { R.info(label, t.text).bad = t.bad; }

void backRow(Rows& R) {
    R.push("Back", "", [] {
        N.view = "main";
        resetRow();
    });
}

void mainRows(Rows& R) {
    if (!N.st.is_object() && N.stErr.empty()) R.info("Network", "Checking...").dim = true;
    else if (!N.stErr.empty() && !N.st.is_object()) R.info("Network", N.stUnsup ? AV : N.stErr);
    if (canControl())
        R.tog("Connect to the Internet", [] { return jbool(N.st, "networking"); }, [](bool v) {
            act("networking", json{{"on", v}}, v ? "Turning on the internet connection..." : "Turning off the internet connection...",
                [v](const json&) { return v ? "Connect to the Internet is on." : "Connect to the Internet is off."; });
        });
    else
        R.push("Connect to the Internet", N.st.is_object() ? std::string(jbool(N.st, "connected") ? "On" : "Off") + " \xC2\xB7 " + AV : AV, [] { toast(AV); });
    R.push("Set Up Internet Connection", N.busy, [] {
        N.view = "setup";
        resetRow();
        scan(true);
    }).dim = !N.busy.empty();
    R.push("Test Internet Connection", N.test.running ? "Testing..." : "", [] {
        N.view = "test";
        resetRow();
        runTest();
    }).dim = N.test.running;
    if (!canControl() && N.st.is_object()) R.note("Windows manages this PC's connection. Turning the connection off, connecting and disconnecting are available in AnyPS5 OS.");
}

void setupRows(Rows& R) {
    backRow(R);
    if (!N.busy.empty()) R.info("Status", N.busy).dim = true;
    R.sh("Wired");
    if (!N.st.is_object()) R.info("LAN Cable", "Checking...").dim = true;
    else {
        json lan = N.st.contains("lan") ? N.st["lan"] : json();
        if (!lan.is_object() || !jbool(lan, "present")) R.info("LAN Cable", "No LAN Port");
        else {
            int sp = lan.value("speedMbps", 0);
            R.info("LAN Cable", jbool(lan, "connected") ? "Connected" + (sp ? " \xC2\xB7 " + std::to_string(sp) + " Mbps" : std::string()) : "Not Connected");
        }
    }
    R.sh("Wi-Fi");
    if (canControl() && jbool(N.st, "wifiHw"))
        R.tog("Wi-Fi", [] { return N.st.value("wifiRadio", true) != false; }, [](bool v) {
            act("radio", json{{"on", v}}, v ? "Turning on Wi-Fi..." : "Turning off Wi-Fi...", [v](const json&) { return v ? "Wi-Fi is on." : "Wi-Fi is off."; });
        });
    R.push("Search Again", N.scanning ? "Searching..." : "", [] { scan(true); }).dim = N.scanning;
    const json& w = N.wifi;
    bool have = w.is_object();
    json nets = have && w.contains("networks") && w["networks"].is_array() ? w["networks"] : json::array();
    if (N.scanning && !have) R.info("Networks Found", "Searching...").dim = true;
    else if (!N.wifiErr.empty()) R.info("Networks Found", N.wifiUnsup ? AV : N.wifiErr);
    else if (have && !jbool(w, "present")) R.info("Wi-Fi", "No Wi-Fi Device");
    else if (have && !jbool(w, "radio")) R.info("Wi-Fi", "Off");
    else if (have && nets.empty()) R.info("Networks Found", "None");
    else if (have)
        for (auto& n : nets) {
            json net = n;
            int sig = n.contains("signal") && n["signal"].is_number() ? n["signal"].get<int>() : -2;
            Row& row = canControl() ? R.push(jstr(n, "ssid"), jbool(n, "inUse") ? "Connected" : "", [net] { pickNetwork(net); })
                                    : R.info(jstr(n, "ssid"), jbool(n, "inUse") ? "Connected" : "");
            row.bars = sig;
            row.lock = jbool(n, "secured");
        }
    R.push("Set Up Manually", canControl() ? "" : AV, setUpManually);
    if (!canControl() && have && jbool(w, "present")) R.note("This list is read-only on Windows. Connecting to a network is available in AnyPS5 OS.");
}

void testRows(Rows& R) {
    backRow(R);
    if (!N.test.on) {
        R.push("Start Test", "", runTest);
        return;
    }
    testInfo(R, "Obtain IP Address", N.test.ip);
    testInfo(R, "Internet Connection", N.test.net);
    testInfo(R, "Download Speed", N.test.down);
    testInfo(R, "Upload Speed", N.test.up);
    if (!N.test.why.empty()) R.note(N.test.why);
    if (!N.test.running) R.push("Test Again", "", runTest);
}

void statusRows(Rows& R, bool withBack) {
    if (withBack) backRow(R);
    R.push("Check Again", N.loading ? "Checking..." : "", loadStatus).dim = N.loading;
    if (!N.st.is_object()) {
        R.info("Connection Method", !N.stErr.empty() ? (N.stUnsup ? AV : N.stErr) : "Checking...");
        return;
    }
    const json& s = N.st;
    const std::string none = "\xE2\x80\x94";
    auto sec = [&](const std::string& label, const std::string& v) {
        Row& r = R.info(label, v.empty() ? none : v);
        r.secret = !v.empty();
        r.dim = v.empty();
    };
    R.info("Internet Connection", jbool(s, "internet") ? "Connected" : jbool(s, "connected") ? "Not Connected to the Internet" : "Not Connected");
    std::string method = jstr(s, "method");
    R.info("Connection Method", method.empty() ? "Not Connected" : method);
    if (method == "Wi-Fi") {
        sec("SSID", jstr(s, "ssid"));
        if (s.contains("signal") && s["signal"].is_number()) {
            Row& r = R.info("Signal Strength", std::to_string(s["signal"].get<int>()) + "%");
            r.bars = s["signal"].get<int>();
            r.barsLeft = true;
        } else R.info("Signal Strength", none).dim = true;
        if (!jstr(s, "band").empty()) R.info("Frequency Band", jstr(s, "band"));
        if (!jstr(s, "security").empty()) R.info("Security Method", jstr(s, "security"));
    }
    sec("IP Address", jstr(s, "ip"));
    std::string mask = jstr(s, "mask");
    R.info("Subnet Mask", mask.empty() ? none : mask).dim = mask.empty();
    sec("Default Gateway", jstr(s, "gateway"));
    json dns = s.contains("dns") && s["dns"].is_array() ? s["dns"] : json::array();
    sec("Primary DNS", dns.size() > 0 && dns[0].is_string() ? dns[0].get<std::string>() : "");
    sec("Secondary DNS", dns.size() > 1 && dns[1].is_string() ? dns[1].get<std::string>() : "");
    sec(method == "Wi-Fi" ? "MAC Address (Wi-Fi)" : "MAC Address (LAN)", jstr(s, "mac"));
}

OsArea make() {
    OsArea a;
    a.id = "network";
    a.cat = "network";
    a.title = "Network";
    a.catTitle = "Network";
    a.newCat = true;
    a.after = "access";
    a.order = 10;
    a.sections = [] {
        return std::vector<Section>{{"Settings", "", [](Rows& R) {
                                         if (N.view == "setup") setupRows(R);
                                         else if (N.view == "test") testRows(R);
                                         else if (N.view == "status") statusRows(R, true);
                                         else mainRows(R);
                                     }},
            {"Connection Status", "", [](Rows& R) {
                 R.sh("View Connection Status");
                 statusRows(R, false);
             }}};
    };
    a.refresh = [] {
        N.view = "main";
        loadStatus();
    };
    return a;
}

}

ANYPS5_OS_AREA(network, make);

}
