#pragma once
#include <string>
#include <vector>

namespace platform {

std::vector<std::string> fontPaths(int weight);
std::vector<std::string> fallbackFontPaths();
void preferVideoDriver();
bool fileExists(const std::string& path);

int drmMaster(int fd);
int hotkeysOpen();
void hotkeysClose();
int hotkeysPoll();
const char* hotkeysLast();
bool isPsKey(int rawKey);
bool overviewKey();

using Socket = long long;
constexpr Socket kNoSocket = -1;
bool netInit();
Socket netConnect(const std::string& host, int port, int timeoutMs);
bool netSend(Socket s, const std::string& data);
int netRecv(Socket s, char* buf, int len, int timeoutMs);
void netShutdown(Socket s);
void netClose(Socket s);

}
