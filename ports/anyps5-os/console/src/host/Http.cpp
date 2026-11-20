#include "host/Http.h"
#include "platform/Platform.h"
#include <cctype>
#include <cstdlib>

namespace host {

Url Url::parse(const std::string& base) {
    Url u;
    std::string s = base;
    auto p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    auto colon = s.rfind(':');
    if (colon != std::string::npos) {
        u.host = s.substr(0, colon);
        u.port = std::atoi(s.substr(colon + 1).c_str());
    } else if (!s.empty()) {
        u.host = s;
        u.port = 80;
    }
    return u;
}

std::string urlEncodePath(const std::string& p) {
    static const char* hexd = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == '?' || c == '=' || c == '&') o += (char)c;
        else {
            o += '%';
            o += hexd[c >> 4];
            o += hexd[c & 15];
        }
    }
    return o;
}

static std::string head(const Url& u, const std::string& method, const std::string& path, const std::string& body) {
    std::string h = method + " " + path + " HTTP/1.1\r\nHost: " + u.authority() + "\r\nConnection: close\r\nUser-Agent: anyps5-console\r\n";
    if (method == "POST") {
        h += "Origin: http://" + u.authority() + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    }
    if (path == "/events") h += "Accept: text/event-stream\r\nCache-Control: no-store\r\n";
    return h + "\r\n" + body;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string dechunk(const std::string& b) {
    std::string out;
    size_t i = 0;
    while (i < b.size()) {
        size_t e = b.find("\r\n", i);
        if (e == std::string::npos) break;
        size_t n = std::strtoul(b.substr(i, e - i).c_str(), nullptr, 16);
        if (n == 0) break;
        out += b.substr(e + 2, n);
        i = e + 2 + n + 2;
    }
    return out;
}

Response request(const Url& u, const std::string& method, const std::string& path, const std::string& body, int timeoutMs) {
    Response r;
    auto s = platform::netConnect(u.host, u.port, 3000);
    if (s == platform::kNoSocket) return r;
    if (!platform::netSend(s, head(u, method, urlEncodePath(path), body))) {
        platform::netClose(s);
        return r;
    }
    std::string raw;
    char buf[16384];
    for (;;) {
        int n = platform::netRecv(s, buf, sizeof buf, timeoutMs);
        if (n <= 0) break;
        raw.append(buf, (size_t)n);
    }
    platform::netClose(s);
    auto he = raw.find("\r\n\r\n");
    if (he == std::string::npos) return r;
    std::string hdr = raw.substr(0, he);
    r.body = raw.substr(he + 4);
    auto sp = hdr.find(' ');
    if (sp != std::string::npos) r.status = std::atoi(hdr.c_str() + sp + 1);
    std::string lh = lower(hdr);
    auto ct = lh.find("\r\ncontent-type:");
    if (ct != std::string::npos) {
        auto e = lh.find("\r\n", ct + 2);
        r.contentType = lh.substr(ct + 15, e - ct - 15);
    }
    if (lh.find("transfer-encoding: chunked") != std::string::npos) r.body = dechunk(r.body);
    return r;
}

bool stream(const Url& u, const std::string& path, const std::atomic<bool>& stop, const std::function<void(const std::string&)>& onLine) {
    auto s = platform::netConnect(u.host, u.port, 3000);
    if (s == platform::kNoSocket) return false;
    if (!platform::netSend(s, head(u, "GET", path, ""))) {
        platform::netClose(s);
        return false;
    }
    std::string pending;
    bool inBody = false, ok = false;
    char buf[8192];
    while (!stop) {
        int n = platform::netRecv(s, buf, sizeof buf, 500);
        if (n == -2) continue;
        if (n <= 0) break;
        pending.append(buf, (size_t)n);
        if (!inBody) {
            auto he = pending.find("\r\n\r\n");
            if (he == std::string::npos) continue;
            ok = pending.compare(0, 12, "HTTP/1.1 200") == 0 || pending.compare(0, 12, "HTTP/1.0 200") == 0;
            if (!ok) break;
            pending.erase(0, he + 4);
            inBody = true;
        }
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, nl + 1);
            onLine(line);
        }
    }
    platform::netClose(s);
    return ok;
}

}
