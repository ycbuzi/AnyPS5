#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

extern "C" void* APS5_VABI memmem_nid_postfix(const void*, std::size_t, const void*, std::size_t);
extern "C" int* APS5_VABI __error_nid_postfix();

static void Require(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "Memmem check failed at line %d\n", line);
        std::abort();
    }
}
#define Check(value) Require((value), __LINE__)

class Guarded {
    unsigned char* base;
    std::size_t size;
public:
    Guarded() {
#ifdef _WIN32
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        size = info.dwPageSize;
        base = static_cast<unsigned char*>(VirtualAlloc(nullptr, size * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(base != nullptr);
        DWORD previous;
        Check(VirtualProtect(base + size, size, PAGE_NOACCESS, &previous));
#else
        const auto pageSize = sysconf(_SC_PAGESIZE);
        Check(pageSize > 0);
        size = static_cast<std::size_t>(pageSize);
        base = static_cast<unsigned char*>(mmap(nullptr, size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        Check(base != MAP_FAILED);
        Check(mprotect(base + size, size, PROT_NONE) == 0);
#endif
    }
    ~Guarded() {
#ifdef _WIN32
        Check(VirtualFree(base, 0, MEM_RELEASE));
#else
        Check(munmap(base, size * 2) == 0);
#endif
    }
    unsigned char* End() const { return base + size; }
};

static void Compare(const void* haystack, std::size_t length, const void* needle, std::size_t needleLength) {
    const std::string text(static_cast<const char*>(haystack), length);
    const std::string pattern(static_cast<const char*>(needle), needleLength);
    const auto expected = text.find(pattern);
    *__error_nid_postfix() = 71;
    const auto* result = memmem_nid_postfix(haystack, length, needle, needleLength);
    Check(*__error_nid_postfix() == 71);
    Check(result == (expected == std::string::npos ? nullptr : static_cast<const unsigned char*>(haystack) + expected));
}

int main() {
    Check(memmem_nid_postfix(nullptr, 0, nullptr, 0) == nullptr);
    Check(memmem_nid_postfix(nullptr, 0, "x", 1) == nullptr);
    const unsigned char data[]{0xff, 0, 0x80, 0, 0x80, 0xff};
    Check(memmem_nid_postfix(data, sizeof(data), nullptr, 0) == data);
    Compare(data, sizeof(data), data + 1, 2);
    Compare(data, 2, data + 1, 2);
    Compare("aaaaab", 6, "aaab", 4);
    Compare("aaaaab", 6, "aaac", 4);
    std::uint32_t seed = 0x6ac1572u;
    const auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    for (unsigned test = 0; test < 5000; ++test) {
        std::string text(next() % 128, '\0');
        std::string pattern(next() % 40, '\0');
        for (char& value : text) value = static_cast<char>(next() >> 24);
        for (char& value : pattern) value = static_cast<char>(next() >> 24);
        if (!pattern.empty() && pattern.size() <= text.size() && test % 2 == 0) {
            const auto offset = next() % (text.size() - pattern.size() + 1);
            text.replace(offset, pattern.size(), pattern);
        }
        Compare(text.data(), text.size(), pattern.data(), pattern.size());
    }
    Guarded text, pattern;
    for (std::size_t length = 0; length <= 32; ++length) {
        auto* haystack = text.End() - length;
        for (std::size_t index = 0; index < length; ++index) haystack[index] = static_cast<unsigned char>(index % 3);
        for (std::size_t patternLength = 0; patternLength <= 16; ++patternLength) {
            auto* needle = pattern.End() - patternLength;
            for (std::size_t index = 0; index < patternLength; ++index) needle[index] = static_cast<unsigned char>(index % 3);
            Compare(haystack, length, needle, patternLength);
            if (patternLength != 0) {
                needle[patternLength - 1] = 0xff;
                Compare(haystack, length, needle, patternLength);
            }
        }
    }
}
