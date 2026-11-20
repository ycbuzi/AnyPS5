#include "platform/Platform.h"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform {

bool netInit() {
    signal(SIGPIPE, SIG_IGN);
    return true;
}

Socket netConnect(const std::string& host, int port, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return kNoSocket;
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            if (poll(&p, 1, timeoutMs) == 1) {
                int err = 0;
                socklen_t l = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
                rc = err == 0 ? 0 : -1;
            } else {
                rc = -1;
            }
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, fl);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd < 0 ? kNoSocket : (Socket)fd;
}

bool netSend(Socket s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = send((int)s, data.data() + off, data.size() - off, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

int netRecv(Socket s, char* buf, int len, int timeoutMs) {
    pollfd p{(int)s, POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (r == 0) return -2;
    if (r < 0) return errno == EINTR ? -2 : -1;
    ssize_t n = recv((int)s, buf, (size_t)len, 0);
    if (n < 0) return errno == EINTR || errno == EAGAIN ? -2 : -1;
    return (int)n;
}

void netShutdown(Socket s) {
    if (s != kNoSocket) shutdown((int)s, SHUT_RDWR);
}

void netClose(Socket s) {
    if (s != kNoSocket) close((int)s);
}

}
