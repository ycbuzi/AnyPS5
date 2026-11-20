#pragma once
#include "host/Http.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace host {

using json = nlohmann::json;
using Reply = std::function<void(const json&)>;

class HostClient {
public:
    ~HostClient();
    bool startLive(const std::string& baseUrl);
    bool loadFixture(const std::string& path);
    bool isFixture() const { return fixtureMode_; }
    const json& fixture() const { return fixture_; }
    std::string fixtureDir() const { return fixtureDir_; }

    void send(const json& msg, Reply done = nullptr);
    void sys(const std::string& area, const std::string& action, const json& args, Reply done);
    void get(const std::string& path, std::function<void(int status, const std::string& body)> done);
    std::optional<std::string> fetchBytes(const std::string& pathOrUrl);
    void pollUi(const std::function<void(const json&)>& onMessage);
    void inject(const json& msg);
    void stop();
    const std::vector<json>& sentLog() const { return sent_; }

private:
    void worker();
    void events();
    void post(std::function<void()> job);
    void toUi(std::function<void()> fn);

    Url url_;
    bool fixtureMode_ = false;
    json fixture_;
    std::string fixtureDir_;
    std::vector<json> sent_;
    std::atomic<bool> stop_{false};
    std::thread worker_, events_;
    std::mutex jobsM_, uiM_;
    std::condition_variable jobsCv_;
    std::deque<std::function<void()>> jobs_;
    std::deque<std::function<void()>> ui_;
    std::deque<json> msgs_;
};

}
