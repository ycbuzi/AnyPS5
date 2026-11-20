#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ui::trophies {

using json = nlohmann::json;

extern const std::array<const char*, 4> kGrades;

struct Trophy {
    int id = 0;
    std::string grade;
    bool hidden = false;
    int group = -1;
    std::string name, detail, icon;
    json e;
    int64_t earned = 0;
};

struct Stats {
    int total = 0, earned = 0, pts = 0, max = 0, pct = 0;
    int64_t last = 0;
    bool hasPlat = false, plat = false;
    std::array<int, 4> got{}, all{};
};

struct Set {
    std::string key, id, title, icon;
    int label = 0;
    std::vector<Trophy> trophies;
    json groups;
    Stats st;
};

struct Level {
    int level = 1, into = 0, step = 1;
};

int gradeIndex(const std::string& g);
int gradePoints(const std::string& g);
std::string gradeName(const std::string& g);
uint32_t gradeColor(const std::string& g);
uint32_t levelColor(int level);
Level level(int points);
Stats stats(const Set& s);

class Store {
public:
    bool loaded() const { return loaded_; }
    void reload();
    void parse(const std::string& body);
    void applyUser();
    std::vector<Set>& games() { return games_; }
    const Set* byKey(const std::string& key);
    const Set* forGame(const std::string& gameId);
    std::vector<const Set*> sets(const std::string& sort);
    std::vector<const Trophy*> list(const Set& s, const std::string& sort) const;

private:
    std::vector<Set> games_;
    bool loaded_ = false;
    std::string user_ = "\x01";
    uint64_t version_ = 0, applied_ = ~0ull;
};

Store& store();
std::string when(int64_t ms);
int64_t parseLocal(const std::string& ymdhm);

}
