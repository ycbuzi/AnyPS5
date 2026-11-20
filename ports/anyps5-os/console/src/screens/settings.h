#pragma once
#include "core/App.h"
#include <functional>
#include <string>
#include <vector>

namespace ui::sett {

using json = nlohmann::json;

inline const char* AV = "Available in AnyPS5 OS";

struct Row {
    std::string label, val, icon, valIcon;
    bool head = false, note = false, info = false;
    bool secret = false, bad = false, dim = false, lock = false, barsLeft = false;
    int swatch = -1;
    int bars = -1;
    std::function<bool()> toggle;
    std::function<void()> run;
    bool selectable() const { return !head && !note && !info; }
};

struct Rows : std::vector<Row> {
    Row& tog(const std::string& label, std::function<bool()> get, std::function<void(bool)> set);
    Row& info(const std::string& label, const std::string& val = "");
    Row& sh(const std::string& t);
    Row& note(const std::string& t);
    Row& push(const std::string& label, const std::string& val, std::function<void()> run);
};

struct Section {
    std::string name;
    std::string user;
    std::function<void(Rows&)> rows;
};

struct OsArea {
    std::string id, cat, title, catTitle, icon, after;
    bool newCat = false;
    int order = 0;
    std::function<std::vector<Section>()> sections;
    std::function<void()> refresh;
    std::function<void(bool force)> tick;
};

struct Cat {
    std::string id, title;
};

std::vector<OsArea>& osAreas();
bool addOsArea(OsArea a);
bool areaOn(const std::string& id);
std::vector<Cat> allCats();
std::string catIcon(const std::string& id);
std::vector<Section> catSections(const std::string& id);
std::vector<Section> baseSections(const std::string& id);
void platformRows(Rows& R);
std::vector<Section> usersSections();
std::vector<Section> accessSections();
std::vector<Section> systemSections();

bool onPage(const std::string& cat);
std::string pageId();
void openSettings();
void openPage(const std::string& id, const std::string& title, const std::string& userUid = "");
void resetRow();
Rect anchor();
void setAnchor(Rect r);

struct MenuItem {
    std::string label;
    std::function<void()> fn;
    bool check = false;
    std::string right;
    bool sep = false;
    MenuItem(std::string l, std::function<void()> f, bool c = false, std::string r = "", bool s = false)
        : label(std::move(l)), fn(std::move(f)), check(c), right(std::move(r)), sep(s) {}
};
void menu(std::vector<MenuItem> items, bool anchored = false);

struct OskOpts {
    int max = 24;
    bool capFirst = true;
    bool password = false;
};
void osk(const std::string& title, const std::string& text, std::function<void(const std::string&)> done, OskOpts o = {});

void sys(const std::string& area, const std::string& action, const json& args, std::function<void(const json&)> done);
void saveApply();
void toast(const std::string& t);
bool familyRestricted(const json& p);
float tsz(float remSize);
float textScale();
json* profileByUid(const std::string& uid);
std::string jstr(const json& o, const char* k, const std::string& d = "");
bool jbool(const json& o, const char* k, bool d = false);
std::string failText(const json& r, const std::string& what = "");
std::string langName(const std::string& tag, const std::string& fallback = "");
bool dnd();
void setDnd(bool on);

}

#define ANYPS5_OS_AREA(Name, Make) static const bool Name##_os_area = ::ui::sett::addOsArea(Make())
