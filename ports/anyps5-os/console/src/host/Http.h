#pragma once
#include <atomic>
#include <functional>
#include <string>

namespace host {

struct Url {
    std::string host = "127.0.0.1";
    int port = 8790;
    std::string authority() const { return host + ":" + std::to_string(port); }
    static Url parse(const std::string& base);
};

struct Response {
    int status = 0;
    std::string body;
    std::string contentType;
};

Response request(const Url& u, const std::string& method, const std::string& path, const std::string& body, int timeoutMs = 15000);
bool stream(const Url& u, const std::string& path, const std::atomic<bool>& stop, const std::function<void(const std::string& line)>& onLine);
std::string urlEncodePath(const std::string& p);

}
