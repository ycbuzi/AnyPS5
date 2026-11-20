#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ui::collection {

enum class State { Unknown, Installed, NotInstalled, Installing, Failed, Unsupported };

struct Source {
    std::string type, label, version;
};

struct Item {
    std::string id, title, icon, bg, started, status;
    State state = State::Unknown;
    int pct = -1;
    std::string version, phase, source, actionId, actionLabel, actionReason, compat, compatReason, updateReason;
    long long size = -1;
    bool actionEnabled = false, update = false, installed = false;
    std::vector<Source> sources;
    nlohmann::json build;
    nlohmann::json raw;
};

bool supported();
bool loaded();
bool loading();
const std::vector<Item>& items();
const Item* byId(const std::string& id);
std::string label(const Item& it);
std::string labelFor(const std::string& id);
std::string server();
std::string listError();
const nlohmann::json& device();

std::string sourceName(const Source& s);
std::string sourcesText(const Item& it);
std::string sizeText(long long bytes);
std::string shortCommit(const std::string& c);
std::string compatText(const Item& it);
std::string buildText(const Item& it);
std::string deviceText();
std::string actionVerb(const Item& it);
std::string sentence(std::string s);

void refresh();
void install(const std::string& id);
void remove(const std::string& id);
void reinstall(const std::string& id);
void cancel(const std::string& id);
void act(const std::string& id);
void setServer(const std::string& address);
void onHost(const nlohmann::json& m, const void* who);
nlohmann::json downloads();

State parseState(const nlohmann::json& j);
std::vector<Item> parseItems(const nlohmann::json& data);

}
