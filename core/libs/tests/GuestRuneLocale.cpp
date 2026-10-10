#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>

extern "C" {
extern const unsigned char _DefaultRuneLocale_nid_postfix[4224];
extern const unsigned char* _CurrentRuneLocale_nid_postfix;
extern int __mb_sb_limit_nid_postfix;
int APS5_VABI isalpha_nid_postfix(int);
int APS5_VABI iscntrl_nid_postfix(int);
int APS5_VABI isdigit_nid_postfix(int);
int APS5_VABI isgraph_nid_postfix(int);
int APS5_VABI islower_nid_postfix(int);
int APS5_VABI isprint_nid_postfix(int);
int APS5_VABI ispunct_nid_postfix(int);
int APS5_VABI isspace_nid_postfix(int);
int APS5_VABI isupper_nid_postfix(int);
int APS5_VABI isxdigit_nid_postfix(int);
int APS5_VABI isblank_nid_postfix(int);
int APS5_VABI tolower_nid_postfix(int);
int APS5_VABI toupper_nid_postfix(int);
}

void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::fprintf(stderr, "Rune locale check failed at line %u\n", location.line());
        std::abort();
    }
}

template <typename T>
T Field(const unsigned char* locale, std::size_t offset) {
    T value;
    std::memcpy(&value, locale + offset, sizeof(value));
    return value;
}

int main() {
    const unsigned char* locale = _DefaultRuneLocale_nid_postfix;
    Require(_CurrentRuneLocale_nid_postfix == locale);
    Require(__mb_sb_limit_nid_postfix == 256);
    Require(std::memcmp(locale, "RuneMagi", 8) == 0);
    Require(std::strcmp(reinterpret_cast<const char*>(locale + 8), "NONE") == 0);
    Require(Field<void*>(locale, 40) == nullptr && Field<void*>(locale, 48) == nullptr);
    Require(Field<std::int32_t>(locale, 56) == 0xfffd);
    for (std::size_t offset = 4160; offset < 4224; ++offset) Require(locale[offset] == 0);
    const struct {
        std::uint64_t bit;
        int (APS5_VABI* classify)(int);
    } classes[] = {
        {0x100, isalpha_nid_postfix}, {0x200, iscntrl_nid_postfix}, {0x400, isdigit_nid_postfix},
        {0x800, isgraph_nid_postfix}, {0x1000, islower_nid_postfix}, {0x2000, ispunct_nid_postfix},
        {0x4000, isspace_nid_postfix}, {0x8000, isupper_nid_postfix}, {0x10000, isxdigit_nid_postfix},
        {0x20000, isblank_nid_postfix}, {0x40000, isprint_nid_postfix},
    };
    for (int c = 0; c < 256; ++c) {
        const auto type = Field<std::uint64_t>(locale, 64 + 8 * c);
        for (const auto& entry : classes) Require(((type & entry.bit) != 0) == (entry.classify(c) != 0));
        Require(((type & 0x400000) != 0) == (isdigit_nid_postfix(c) != 0));
        Require((type & ~std::uint64_t{0x47ffff}) == 0);
        if (isxdigit_nid_postfix(c)) {
            const int value = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
            Require(static_cast<int>(type & 0xff) == value);
        } else {
            Require((type & 0xff) == 0);
        }
        Require(Field<std::int32_t>(locale, 2112 + 4 * c) == tolower_nid_postfix(c));
        Require(Field<std::int32_t>(locale, 3136 + 4 * c) == toupper_nid_postfix(c));
    }
    Require(Field<std::uint64_t>(locale, 64 + 8 * 0x80) == 0 && Field<std::uint64_t>(locale, 64 + 8 * 0xff) == 0);
    return 0;
}
