#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

std::wstring g_folder;
std::string g_json = "{";

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\netin.json").c_str(), L"wb")) {
        const std::string closed = g_json + "\"end\":1}";
        std::fwrite(closed.data(), 1, closed.size(), f);
        std::fclose(f);
    }
}

void Put(const std::string& key, const std::string& value) {
    g_json += "\"" + key + "\":" + value + ",";
    Save();
}

int ReadPort() {
    int port = 50555;
    if (FILE* f = _wfopen((g_folder + L"\\netin.txt").c_str(), L"rb")) {
        std::fscanf(f, "%d", &port);
        std::fclose(f);
    }
    return port;
}

void Serve(SOCKET listener, int seconds) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(listener, &r);
    timeval tv{seconds, 0};
    const int ready = select(0, &r, nullptr, nullptr, &tv);
    if (ready <= 0) return Put("inbound", "\"no connection\"");
    SOCKET c = accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET) return Put("inbound", std::to_string(WSAGetLastError()));
    char hello[64]{};
    const int got = recv(c, hello, sizeof hello - 1, 0);
    std::vector<char> block(1 << 20, 'x');
    long long sent = 0;
    const ULONGLONG t0 = GetTickCount64();
    while (sent < (64ll << 20)) {
        const int k = send(c, block.data(), static_cast<int>(block.size()), 0);
        if (k <= 0) break;
        sent += k;
    }
    const double secs = (GetTickCount64() - t0) / 1000.0;
    char b[160];
    std::snprintf(b, sizeof b, "{\"hello_bytes\":%d,\"sent_mb\":%lld,\"mb_s\":%.1f}", got, sent >> 20, secs > 0 ? (sent / 1048576.0) / secs : 0.0);
    Put("inbound", b);
    closesocket(c);
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    Put("started", "1");
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        Put("startup", std::to_string(WSAGetLastError()));
    } else {
        const int port = ReadPort();
        Put("port", std::to_string(port));
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<u_short>(port));
        int rc = bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
        Put("bind", std::to_string(rc == 0 ? 0 : WSAGetLastError()));
        if (rc == 0) {
            rc = listen(s, 4);
            Put("listen", std::to_string(rc == 0 ? 0 : WSAGetLastError()));
            if (rc == 0) Serve(s, 75);
        }
        closesocket(s);
        WSACleanup();
    }
    const std::string done = "{\"netin\":\"netin.json\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
