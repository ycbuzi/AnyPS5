#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#endif
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

struct GuestInterfaceAddress {
    GuestInterfaceAddress* next;
    char* name;
    std::uint32_t flags;
    void* address;
    void* netmask;
    void* destination;
    void* data;
};
static_assert(sizeof(GuestInterfaceAddress) == 56);

extern "C" {
int APS5_VABI getifaddrs_nid_postfix(GuestInterfaceAddress** list);
void APS5_VABI freeifaddrs_nid_postfix(GuestInterfaceAddress* list);
}

static void Require(bool value) { if (!value) std::abort(); }

static std::size_t HostIpv4Count() {
    std::size_t count = 0;
#ifdef _WIN32
    const ULONG options = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0;
    std::vector<std::uint8_t> storage;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, options, nullptr, nullptr, &size);
    while (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC, options, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    }
    Require(result == NO_ERROR);
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); adapter; adapter = adapter->Next)
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            if (unicast->Address.lpSockaddr && unicast->Address.lpSockaddr->sa_family == AF_INET) ++count;
#else
    ifaddrs* host = nullptr;
    Require(getifaddrs(&host) == 0);
    for (auto* item = host; item; item = item->ifa_next)
        if (item->ifa_addr && item->ifa_addr->sa_family == AF_INET) ++count;
    freeifaddrs(host);
#endif
    return count;
}

int main() {
    constexpr std::uint32_t knownFlags = 0x1 | 0x2 | 0x8 | 0x10 | 0x40 | 0x80 | 0x100 | 0x200 | 0x8000;
    const std::uint8_t loopbackAddress[] = {127, 0, 0, 1};
    const std::uint8_t loopbackMask[] = {255, 0, 0, 0};
    GuestInterfaceAddress* list = nullptr;
    Require(getifaddrs_nid_postfix(&list) == 0 && list != nullptr);
    std::size_t entries = 0;
    std::size_t ipv4 = 0;
    bool loopback = false;
    for (auto* item = list; item; item = item->next) {
        Require(++entries < 100000);
        Require(item->name != nullptr);
        const std::size_t nameLength = std::strlen(item->name);
        Require(nameLength >= 1 && nameLength <= 15);
        Require((item->flags & ~knownFlags) == 0);
        Require(item->data == nullptr);
        const auto* address = static_cast<const std::uint8_t*>(item->address);
        const auto* netmask = static_cast<const std::uint8_t*>(item->netmask);
        Require(address != nullptr && netmask != nullptr);
        Require((address[0] == 16 && address[1] == 2) || (address[0] == 28 && address[1] == 28));
        Require(netmask[0] == address[0] && netmask[1] == address[1]);
        if (item->destination) {
            const auto* destination = static_cast<const std::uint8_t*>(item->destination);
            Require(destination[0] == address[0] && destination[1] == address[1]);
        }
        if (address[1] != 2) continue;
        ++ipv4;
        if (std::memcmp(address + 4, loopbackAddress, 4) == 0)
            loopback = std::memcmp(netmask + 4, loopbackMask, 4) == 0 && (item->flags & 0x9) == 0x9 && !(item->flags & 0x2);
    }
    Require(loopback);
    Require(ipv4 == HostIpv4Count());
    freeifaddrs_nid_postfix(list);
    freeifaddrs_nid_postfix(nullptr);
    bool rejected = false;
    try {
        getifaddrs_nid_postfix(nullptr);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected);
}
