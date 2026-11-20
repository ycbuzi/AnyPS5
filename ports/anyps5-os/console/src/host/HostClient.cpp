#include "host/HostClient.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <sstream>

namespace host {

HostClient::~HostClient() { stop(); }

void HostClient::stop() {
    if (stop_.exchange(true)) return;
    jobsCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (events_.joinable()) events_.join();
}

bool HostClient::startLive(const std::string& baseUrl) {
    url_ = Url::parse(baseUrl);
    fixtureMode_ = false;
    worker_ = std::thread([this] { worker(); });
    events_ = std::thread([this] { events(); });
    return true;
}

static std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool HostClient::loadFixture(const std::string& path) {
    auto text = readFile(path);
    if (!text) return false;
    try {
        fixture_ = json::parse(*text);
    } catch (const std::exception& e) {
        SDL_Log("fixture %s: %s", path.c_str(), e.what());
        return false;
    }
    fixtureMode_ = true;
    auto slash = path.find_last_of("/\\");
    fixtureDir_ = slash == std::string::npos ? "." : path.substr(0, slash);
    json first = fixture_.value("state", json::object());
    if (!first.contains("host")) first["host"] = true;
    msgs_.push_back(first);
    for (auto& e : fixture_.value("events", json::array())) msgs_.push_back(e);
    return true;
}

void HostClient::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> l(jobsM_);
        jobs_.push_back(std::move(job));
    }
    jobsCv_.notify_one();
}

void HostClient::toUi(std::function<void()> fn) {
    std::lock_guard<std::mutex> l(uiM_);
    ui_.push_back(std::move(fn));
}

void HostClient::worker() {
    while (!stop_) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> l(jobsM_);
            jobsCv_.wait(l, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

void HostClient::events() {
    while (!stop_) {
        std::string data;
        stream(url_, "/events", stop_, [&](const std::string& line) {
            if (line.rfind("data:", 0) == 0) {
                std::string d = line.substr(5);
                if (!d.empty() && d[0] == ' ') d.erase(0, 1);
                data += data.empty() ? d : "\n" + d;
            } else if (line.empty() && !data.empty()) {
                try {
                    json m = json::parse(data);
                    std::lock_guard<std::mutex> l(uiM_);
                    msgs_.push_back(std::move(m));
                } catch (...) {
                }
                data.clear();
            }
        });
        for (int i = 0; i < 20 && !stop_; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void HostClient::send(const json& msg, Reply done) {
    if (fixtureMode_) {
        sent_.push_back(msg);
        SDL_Log("host <- %s", msg.dump().c_str());
        if (done) toUi([done] { done(json{{"ok", true}, {"fixture", true}}); });
        return;
    }
    std::string body = msg.dump();
    post([this, body, done] {
        Response r = request(url_, "POST", "/msg", body);
        json j;
        try { j = json::parse(r.body); } catch (...) { j = json{{"ok", false}, {"error", "no reply"}}; }
        if (done) toUi([done, j] { done(j); });
    });
}

void HostClient::sys(const std::string& area, const std::string& action, const json& args, Reply done) {
    if (fixtureMode_) {
        sent_.push_back(json{{"sys", area}, {"action", action}, {"args", args}});
        auto answers = fixture_.value("sys", json::object());
        std::string key = area + "." + action;
        json r = answers.contains(key) ? answers[key] : json{{"ok", false}, {"unsupported", true}, {"error", "No answer for " + key + " in the fixture"}};
        if (done) toUi([done, r] { done(r); });
        return;
    }
    std::string body = json{{"area", area}, {"action", action}, {"args", args.is_null() ? json::object() : args}}.dump();
    post([this, body, done] {
        Response r = request(url_, "POST", "/sys", body, 45000);
        json j;
        try { j = json::parse(r.body); } catch (...) { j = json{{"ok", false}, {"error", "AnyPS5 did not answer"}}; }
        if (done) toUi([done, j] { done(j); });
    });
}

void HostClient::get(const std::string& path, std::function<void(int, const std::string&)> done) {
    if (fixtureMode_) {
        std::string p = path.substr(0, path.find('?'));
        if (!p.empty() && p[0] == '/') p.erase(0, 1);
        std::optional<std::string> b;
        if (p == "library.js") b = "window.LIBRARY = " + fixture_.value("library", json::array()).dump() + ";";
        else if (p == "cache/media.js") b = "window.MEDIA = " + fixture_.value("media", json::object()).dump() + ";";
        else if (fixture_.contains("files") && fixture_["files"].contains(p)) b = fixture_["files"][p].is_string() ? fixture_["files"][p].get<std::string>() : fixture_["files"][p].dump();
        else b = fetchBytes(p);
        toUi([done, b] { done(b ? 200 : 404, b ? *b : std::string()); });
        return;
    }
    post([this, path, done] {
        Response r = request(url_, "GET", path[0] == '/' ? path : "/" + path, "");
        toUi([done, r] { done(r.status, r.body); });
    });
}

std::optional<std::string> HostClient::fetchBytes(const std::string& p) {
    if (p.empty()) return std::nullopt;
    if (fixtureMode_ || p.rfind("file:", 0) == 0) {
        std::string path = p.rfind("file:", 0) == 0 ? p.substr(5) : p;
        auto direct = readFile(path);
        if (direct) return direct;
        while (!path.empty() && path[0] == '/') path.erase(0, 1);
        return readFile(fixtureDir_ + "/" + path);
    }
    std::string path = p;
    if (path.rfind("http://", 0) == 0) {
        auto s = path.find('/', 7);
        path = s == std::string::npos ? "/" : path.substr(s);
    }
    if (path[0] != '/') path = "/" + path;
    Response r = request(url_, "GET", path, "");
    if (r.status != 200) return std::nullopt;
    return r.body;
}

void HostClient::inject(const json& msg) {
    std::lock_guard<std::mutex> l(uiM_);
    msgs_.push_back(msg);
}

void HostClient::pollUi(const std::function<void(const json&)>& onMessage) {
    std::deque<std::function<void()>> fns;
    std::deque<json> msgs;
    {
        std::lock_guard<std::mutex> l(uiM_);
        fns.swap(ui_);
        msgs.swap(msgs_);
    }
    for (auto& m : msgs) onMessage(m);
    for (auto& f : fns) f();
}

}
