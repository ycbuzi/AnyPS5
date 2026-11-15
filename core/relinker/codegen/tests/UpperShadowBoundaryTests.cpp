#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTypes.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include <codegen/x86/VexLegacyLowering.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__x86_64__) && defined(__linux__)
#include <sys/mman.h>
#define BOUNDARY_EXECUTION_AVAILABLE 1
#else
#define BOUNDARY_EXECUTION_AVAILABLE 0
#endif

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

#if BOUNDARY_EXECUTION_AVAILABLE

alignas(16) thread_local std::uint64_t shadowArea[32];

std::int32_t shadowOffset() {
    std::uint64_t threadPointer = 0;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(threadPointer));
    return static_cast<std::int32_t>(reinterpret_cast<std::uint64_t>(&shadowArea[0]) - threadPointer);
}

const Bytes kStoreYmm1{0xC5, 0xFC, 0x11, 0x0F, 0x48, 0x89, 0xF8, 0xC3};
const Bytes kIndirectCaller{0xC5, 0xFC, 0x10, 0x0E, 0x48, 0x89, 0xFF, 0xFF, 0xD2, 0xC5, 0xF8, 0x77, 0x48, 0x89, 0xC0, 0xC3};
const Bytes kPassThrough{0xC5, 0xE8, 0x57, 0xD2, 0xFF, 0xD1, 0xC3};

class Image {
public:
    Image() {
        _page = static_cast<std::uint8_t*>(mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (_page == MAP_FAILED) throw std::runtime_error("Cannot map the boundary test image");
    }
    ~Image() { munmap(_page, kSize); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    std::size_t Add(const Bytes& function) {
        while (_code.size() % 16 != 0) _code.push_back(0xCC);
        const auto start = _code.size();
        _code.insert(_code.end(), function.begin(), function.end());
        _functions.emplace_back(Address(start), Address(_code.size()));
        return start;
    }

    void Convert() {
        const Domain::ProgramHeader header{1, 5, 0, Address(0), Address(0), _code.size(), _code.size(), 16};
        const auto converted = Codegen::MakeAmd64OnlyConverter(Codegen::Amd64OnlyTarget::Sse, {}, Codegen::UpperShadowContext{shadowOffset(), _functions})->Convert(_code, {header});
        require(converted.KeptCount == 0, "the boundary test image kept a VEX instruction");
        Bytes image = converted.Bytes;
        for (const auto& site : converted.Trampolines) {
            while (image.size() % 16 != 0) image.push_back(0xCC);
            const auto body = image.size();
            image.insert(image.end(), site.Body.begin(), site.Body.end());
            Codegen::ApplyStubRelocations(std::span<std::uint8_t>(image.data() + body, site.ReturnBranchOffset), site.Relocations, site.Address, Address(body), site.Offset);
            const auto back = static_cast<std::int32_t>(static_cast<std::int64_t>(site.Address + site.Length) - static_cast<std::int64_t>(Address(body) + site.ReturnBranchOffset + 5));
            std::memcpy(image.data() + body + site.ReturnBranchOffset + 1, &back, sizeof(back));
            std::fill_n(image.begin() + static_cast<std::ptrdiff_t>(site.Offset), site.Length, 0x90);
            image[site.Offset] = 0xE9;
            const auto jump = static_cast<std::int32_t>(static_cast<std::int64_t>(Address(body)) - static_cast<std::int64_t>(site.Address + 5));
            std::memcpy(image.data() + site.Offset + 1, &jump, sizeof(jump));
        }
        if (image.size() > kSize) throw std::runtime_error("The boundary test image is too large");
        std::memcpy(_page, image.data(), image.size());
        _converted = std::move(image);
    }

    [[nodiscard]] bool HasVex() const {
        for (std::size_t index = 0; index + 1 < _code.size(); ++index) {
            if ((_converted[index] == 0xC4 || _converted[index] == 0xC5) && _converted[index] == _code[index] && Codegen::VexWide(_converted.data() + index, _converted.size() - index)) return true;
        }
        return false;
    }

    [[nodiscard]] void* At(const std::size_t offset) const { return _page + offset; }

private:
    static constexpr std::size_t kSize = 1 << 16;
    std::uint8_t* _page = nullptr;
    Bytes _code;
    Bytes _converted;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> _functions;

    [[nodiscard]] std::uint64_t Address(const std::size_t offset) const { return reinterpret_cast<std::uint64_t>(_page) + offset; }
};

using Entry = void (*)(void* destination, const void* source, void* first, void* second);

void boundaries() {
    Image image;
    const auto store = image.Add(kStoreYmm1);
    const auto indirect = image.Add(kIndirectCaller);
    const auto pass = image.Add(kPassThrough);
    const auto through = image.Add(kIndirectCaller);
    image.Convert();
    require(!image.HasVex(), "a VEX.256 instruction survived the boundary test conversion");
    const std::array<float, 8> source{1.5f, -2.25f, 3.0f, 4.75f, -5.5f, 6.125f, 7.0f, -8.875f};
    const auto check = [&](const std::string& name, const std::size_t entry, void* first, void* second) {
        std::array<float, 8> out{};
        for (auto& slot : shadowArea) slot = 0xDEADBEEFDEADBEEFull;
        reinterpret_cast<Entry>(image.At(entry))(out.data(), source.data(), first, second);
        require(out == source, name + ": the 256-bit value did not reach the callee");
    };
    check("indirect call passing ymm1", indirect, image.At(store), nullptr);
    check("pass-through without VEX.256", through, image.At(pass), image.At(store));
}

#else

void boundaries() { std::cout << "Upper shadow boundary checks require a Linux x86-64 build\n"; }

#endif

}

void unloweredReported() {
    const Bytes code{0xC4, 0xE2, 0x7D, 0x13, 0xCA, 0xC4, 0xE3, 0x79, 0x1D, 0xD1, 0x04, 0xC4, 0xE2, 0x79, 0x13, 0xCA, 0xC5, 0xF8, 0x92, 0xD0, 0xC3};
    const Domain::ProgramHeader header{1, 5, 0, 0, 0, code.size(), code.size(), 16};
    const auto converted = Codegen::MakeAmd64OnlyConverter(Codegen::Amd64OnlyTarget::Sse, {}, std::nullopt)->Convert(code, {header});
    const std::map<std::string, std::size_t> wanted{{"VEX.256.66.0F38.W0 13", 1}, {"VEX.128.66.0F3A.W0 1D", 1}};
    require(converted.Unlowered == wanted, "unlowered F16C forms were not reported");
    const std::map<std::string, std::size_t> masks{{"VEX.128.NP.0F.W0 92", 1}};
    require(converted.Avx512Mask == masks, "the AVX-512 mask instruction was not counted in its own bucket");
}

int main() {
    try {
        unloweredReported();
        boundaries();
        std::cout << "Upper shadow boundary checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
