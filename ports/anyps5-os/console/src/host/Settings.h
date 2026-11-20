#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace host {

class HostClient;

class Settings {
public:
    nlohmann::json S;
    Settings();
    static nlohmann::json defaults();
    bool load(const nlohmann::json& o);
    void save(HostClient& h);
    void ensureUids(HostClient& h);

    nlohmann::json& profiles() { return S["profiles"]; }
    nlohmann::json* profileByUid(const std::string& uid);
    nlohmann::json* profileByName(const std::string& name);
    bool isTestUser(const nlohmann::json& p) const;

    bool a11y(const std::string& k) const;
    nlohmann::json a11yv(const std::string& k, const nlohmann::json& d) const;
    void setA11y(const std::string& k, const nlohmann::json& v);
    nlohmann::json noteOpt(const std::string& k, const nlohmann::json& d) const;
    void setNoteOpt(const std::string& k, const nlohmann::json& v);
    nlohmann::json get(const std::string& k, const nlohmann::json& d) const;
    bool clock24() const;
};

}
