#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>

extern "C" int APS5_VABI gethostname_nid_postfix(char* name, std::size_t length) {
    constexpr char hostname[] = "AnyPS5";
    if (name == nullptr) {
        errno = EINVAL;
        return -1;
    }
    // Report a short buffer without touching it: a partial copy left the caller
    // with an unterminated name that it had no way to detect as truncated.
    if (length < sizeof(hostname)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    std::memcpy(name, hostname, sizeof(hostname));
    return 0;
}
