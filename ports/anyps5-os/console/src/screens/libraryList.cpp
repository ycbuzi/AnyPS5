#include "screens/library.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace ui {

namespace {

using collection::State;

int rank(const LibEntry& e) {
    return e.g.status == "Plays" ? 0 : e.g.status == "Stops early" ? 1 : 2;
}

bool titleLess(const LibEntry& a, const LibEntry& b) {
    return std::lexicographical_compare(a.g.title.begin(), a.g.title.end(), b.g.title.begin(), b.g.title.end(),
                                        [](char x, char y) { return std::tolower((unsigned char)x) < std::tolower((unsigned char)y); });
}

LibEntry fromItem(const collection::Item& it) {
    LibEntry e;
    e.g.id = it.id;
    e.g.title = it.title.empty() ? it.id : it.title;
    e.g.icon = it.icon;
    e.g.bg = it.bg;
    e.g.raw = it.raw;
    e.st = it.state;
    e.pct = it.pct;
    return e;
}

bool installed(const LibEntry& e) {
    if (e.st == State::Installed) return true;
    return e.st == State::Unknown && e.inLib && !e.g.exe.empty();
}

}

bool LibEntry::playable() const {
    return inLib && st != State::NotInstalled && st != State::Installing && st != State::Unsupported;
}

LibEntry collectionEntry(const collection::Item& it) {
    if (const host::Game* g = app().library.byId(it.id)) return libraryEntry(*g);
    return fromItem(it);
}

LibEntry libraryEntry(const host::Game& g) {
    LibEntry e;
    e.g = g;
    e.inLib = true;
    if (const collection::Item* it = collection::byId(g.id); it && !g.id.empty()) {
        e.st = it->state;
        e.pct = it->pct;
    }
    return e;
}

std::vector<LibEntry> libraryEntries(int tab, const std::string& sort) {
    App& A = app();
    std::vector<LibEntry> all;
    std::set<std::string> seen;
    for (auto& g : A.library.all) {
        all.push_back(libraryEntry(g));
        if (!g.id.empty()) seen.insert(g.id);
    }
    if (collection::supported())
        for (auto& it : collection::items())
            if (!seen.count(it.id)) all.push_back(fromItem(it));
    std::vector<LibEntry> out;
    for (auto& e : all) {
        if (tab == 1 && !installed(e)) continue;
        if (tab == 2 && e.g.status != "Plays") continue;
        out.push_back(e);
    }
    if (sort == "az") std::stable_sort(out.begin(), out.end(), titleLess);
    else if (sort == "status")
        std::stable_sort(out.begin(), out.end(), [](const LibEntry& a, const LibEntry& b) {
            int ra = rank(a), rb = rank(b);
            return ra != rb ? ra < rb : titleLess(a, b);
        });
    else
        std::stable_sort(out.begin(), out.end(), [](const LibEntry& a, const LibEntry& b) {
            if (a.g.lastRun != b.g.lastRun) return a.g.lastRun > b.g.lastRun;
            return titleLess(a, b);
        });
    return out;
}

}
