#include "platform/Platform.h"
#if defined(__linux__)
#include <cerrno>
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <drm/drm.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#endif

namespace platform {

#if defined(__linux__)

int drmMaster(int fd) {
    if (fd < 0) return -1;
    drm_auth auth{};
    auth.magic = 0;
    if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth) == 0) return 1;
    return errno == EACCES ? 0 : 1;
}

namespace {

struct Keyboard {
    std::string name;
    int fd;
};

struct Hotkeys {
    std::vector<Keyboard> kbs;
    std::vector<std::string> skipped;
    std::chrono::steady_clock::time_point scanned{};
    bool open = false;
    bool ctrl[2] = {false, false};
    bool alt[2] = {false, false};
    bool down = false;
    const char* last = "";
};

Hotkeys gHk;

bool hasChordKey(int fd) {
    unsigned char bits[KEY_MAX / 8 + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return false;
    auto has = [&](int k) { return (bits[k / 8] >> (k % 8)) & 1; };
    return has(KEY_SCALE) || ((has(KEY_HOME) || has(KEY_ESC)) && (has(KEY_LEFTCTRL) || has(KEY_RIGHTCTRL)));
}

bool known(const std::string& n) {
    for (const Keyboard& k : gHk.kbs)
        if (k.name == n) return true;
    for (const std::string& k : gHk.skipped)
        if (k == n) return true;
    return false;
}

void scan() {
    gHk.scanned = std::chrono::steady_clock::now();
    std::vector<std::string> present;
    if (DIR* d = opendir("/dev/input")) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("event", 0) == 0) present.push_back(n);
        }
        closedir(d);
    }
    std::vector<std::string> stillSkipped;
    for (const std::string& n : gHk.skipped)
        for (const std::string& p : present)
            if (p == n) stillSkipped.push_back(n);
    gHk.skipped = stillSkipped;
    for (const std::string& n : present) {
        if (known(n)) continue;
        int fd = open(("/dev/input/" + n).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (hasChordKey(fd)) gHk.kbs.push_back({n, fd});
        else {
            close(fd);
            gHk.skipped.push_back(n);
        }
    }
}

}

int hotkeysOpen() {
    hotkeysClose();
    gHk.open = true;
    scan();
    return (int)gHk.kbs.size();
}

void hotkeysClose() {
    for (const Keyboard& k : gHk.kbs) close(k.fd);
    gHk = Hotkeys{};
}

int hotkeysPoll() {
    if (!gHk.open) return 0;
    if (std::chrono::steady_clock::now() - gHk.scanned >= std::chrono::seconds(2)) scan();
    int result = 0;
    for (size_t i = 0; i < gHk.kbs.size();) {
        int fd = gHk.kbs[i].fd;
        input_event ev[32];
        ssize_t n;
        while ((n = read(fd, ev, sizeof ev)) > 0) {
            for (ssize_t j = 0; j < n / (ssize_t)sizeof(input_event); j++) {
                const input_event& k = ev[j];
                if (k.type != EV_KEY || k.value == 2) continue;
                bool on = k.value == 1;
                if (k.code == KEY_LEFTCTRL) gHk.ctrl[0] = on;
                else if (k.code == KEY_RIGHTCTRL) gHk.ctrl[1] = on;
                else if (k.code == KEY_LEFTALT) gHk.alt[0] = on;
                else if (k.code == KEY_RIGHTALT) gHk.alt[1] = on;
                else if (k.code == KEY_HOME || k.code == KEY_ESC || k.code == KEY_SCALE) {
                    bool chord = k.code == KEY_SCALE || ((gHk.ctrl[0] || gHk.ctrl[1]) && (gHk.alt[0] || gHk.alt[1]));
                    if (on && chord && !gHk.down) {
                        gHk.down = true;
                        gHk.last = k.code == KEY_SCALE ? "Overview key" : k.code == KEY_HOME ? "Ctrl+Alt+Home" : "Ctrl+Alt+Esc";
                        result = 1;
                    } else if (!on && gHk.down) {
                        gHk.down = false;
                        result = result == 1 ? 2 : -1;
                    }
                }
            }
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close(fd);
            gHk.kbs.erase(gHk.kbs.begin() + (long)i);
            continue;
        }
        i++;
    }
    return result;
}

const char* hotkeysLast() { return gHk.last; }

bool isPsKey(int rawKey) {
    const char* d = SDL_GetCurrentVideoDriver();
    return rawKey == KEY_SCALE && d && std::strcmp(d, "kmsdrm") == 0;
}

bool overviewKey() { return isPsKey(KEY_SCALE); }

#else

int drmMaster(int) { return -1; }
int hotkeysOpen() { return 0; }
void hotkeysClose() {}
int hotkeysPoll() { return 0; }
const char* hotkeysLast() { return ""; }
bool isPsKey(int) { return false; }
bool overviewKey() { return false; }

#endif

}
