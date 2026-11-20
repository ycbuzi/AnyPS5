#include "platform/Platform.h"
#include <winsock2.h>
#include <ws2tcpip.h>

namespace platform {

bool netInit() {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}

Socket netConnect(const std::string& host, int port, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return kNoSocket;
    SOCKET fd = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(fd, FIONBIO, &nb);
        int rc = connect(fd, a->ai_addr, (int)a->ai_addrlen);
        if (rc != 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            WSAPOLLFD p{fd, POLLOUT, 0};
            rc = WSAPoll(&p, 1, timeoutMs) == 1 && !(p.revents & (POLLERR | POLLHUP)) ? 0 : -1;
        }
        if (rc == 0) {
            nb = 0;
            ioctlsocket(fd, FIONBIO, &nb);
            BOOL one = TRUE;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
            break;
        }
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return fd == INVALID_SOCKET ? kNoSocket : (Socket)fd;
}

bool netSend(Socket s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = send((SOCKET)s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

int netRecv(Socket s, char* buf, int len, int timeoutMs) {
    WSAPOLLFD p{(SOCKET)s, POLLIN, 0};
    int r = WSAPoll(&p, 1, timeoutMs);
    if (r == 0) return -2;
    if (r < 0) return -1;
    int n = recv((SOCKET)s, buf, len, 0);
    return n < 0 ? -1 : n;
}

void netShutdown(Socket s) {
    if (s != kNoSocket) shutdown((SOCKET)s, SD_BOTH);
}

void netClose(Socket s) {
    if (s != kNoSocket) closesocket((SOCKET)s);
}

}
