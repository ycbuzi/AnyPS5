#include "screens/settings.h"

namespace ui::sett {

static std::string restText(int a) {
    if (!a) return "Don't Enter Rest Mode";
    if (a >= 60) return std::to_string(a / 60) + (a == 60 ? " Hour" : " Hours");
    return std::to_string(a) + " Minutes";
}

static void powerOff(bool restart) {
    toast(restart ? "Restarting AnyPS5..." : "Turning off AnyPS5...");
    app().host.send(json{{"power", restart ? "restart" : "off"}});
}

static void switchUser() {
    App& A = app();
    if (dnd()) setDnd(false);
    A.me = nullptr;
    A.open("users");
}

std::vector<Section> systemSections() {
    return {{"System Information", "", [](Rows& R) {
                std::string prog = jstr(app().H, "program");
                R.info("Program", prog.empty() ? "AnyPS5 console" : prog);
                R.info("Built", jstr(app().H, "build"));
                platformRows(R);
            }},
        {"Guide and Tips", "", [](Rows& R) {
             R.push("Controls and Help", "", [] { app().open("help"); });
             R.push("Notifications", std::to_string(app().toasts.history().size()), [] { app().open("notifications"); });
         }},
        {"Power Saving", "", [](Rows& R) {
             json aj = app().settings.get("restAfter", 0);
             int a = aj.is_number() ? aj.get<int>() : 0;
             R.push("Enter Rest Mode After", restText(a), [a] {
                 app().settings.S["restAfter"] = a == 0 ? 20 : a == 20 ? 60 : a == 60 ? 180 : 0;
                 app().saveSettings();
             });
             R.note("Counts time without a button press on the AnyPS5 screen. It never rests while a game is running.");
         }},
        {"Power", "", [](Rows& R) {
             R.push("Enter Rest Mode", "", [] {
                 app().host.send(json{{"rest", true}});
                 app().open("rest");
             });
             R.push("Turn Off AnyPS5", "", [] { powerOff(false); });
             R.push("Restart AnyPS5", "", [] { powerOff(true); });
             R.push("Switch User", "", switchUser);
         }}};
}

}
