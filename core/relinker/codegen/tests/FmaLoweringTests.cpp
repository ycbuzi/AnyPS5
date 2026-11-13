#include "VectorExecution.hpp"
#include <codegen/x86/FmaLowering.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/CodegenException.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#if LOWERING_EXECUTION_AVAILABLE
#include <unistd.h>
#endif

namespace {

using LoweringExecution::Bytes;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes site(std::uint8_t opcode, bool wide, bool vector256, std::uint8_t dst, std::uint8_t second, std::uint8_t third) {
    return {0xC4, static_cast<std::uint8_t>(((dst & 8) ? 0 : 0x80) | 0x40 | ((third & 8) ? 0 : 0x20) | 2),
        static_cast<std::uint8_t>((wide ? 0x80 : 0) | ((~second & 15) << 3) | (vector256 ? 4 : 0) | 1), opcode,
        static_cast<std::uint8_t>(0xC0 | ((dst & 7) << 3) | (third & 7))};
}

Bytes memorySite(std::uint8_t opcode, bool wide, bool vector256, std::uint8_t dst, std::uint8_t second) {
    return {0xC4, static_cast<std::uint8_t>(((dst & 8) ? 0 : 0x80) | 0x60 | 2),
        static_cast<std::uint8_t>((wide ? 0x80 : 0) | ((~second & 15) << 3) | (vector256 ? 4 : 0) | 1), opcode,
        static_cast<std::uint8_t>((dst & 7) << 3)};
}

std::vector<std::uint8_t> opcodes() {
    std::vector<std::uint8_t> all;
    for (const std::uint8_t order : {0x90, 0xA0, 0xB0})
        for (std::uint8_t kind = 6; kind < 16; ++kind) all.push_back(static_cast<std::uint8_t>(order | kind));
    return all;
}

bool scalarOpcode(std::uint8_t opcode) {
    return (opcode & 15) >= 8 && (opcode & 1) != 0;
}

void decoding() {
    const Bytes observed{0xC4, 0xE2, 0x51, 0xA8, 0xD9};
    const auto first = Codegen::DecodeFma(observed.data(), observed.size());
    require(first && std::string(first->Name) == "VFMADD213PS" && first->Destination == 3 && first->Second == 5 && first->Third == 1 && !first->Memory && !first->Scalar && !first->Double && !first->Vector256,
        "vfmadd213ps xmm3, xmm5, xmm1 was not decoded");
    const std::array<std::pair<std::uint8_t, const char*>, 8> names{{{0x96, "VFMADDSUB132PD"}, {0xA7, "VFMSUBADD213PD"}, {0xB8, "VFMADD231PD"}, {0x9B, "VFMSUB132SD"},
        {0xAC, "VFNMADD213PD"}, {0xBD, "VFNMADD231SD"}, {0x9E, "VFNMSUB132PD"}, {0xAF, "VFNMSUB213SD"}}};
    for (const auto& [opcode, name] : names) {
        const auto bytes = site(opcode, true, true, 9, 12, 14);
        const auto operands = Codegen::DecodeFma(bytes.data(), bytes.size());
        require(operands && std::string(operands->Name) == name && operands->Double && operands->Destination == 9 && operands->Second == 12 && operands->Third == 14, std::string(name) + " was not decoded");
        require(operands->Scalar == scalarOpcode(opcode) && operands->Vector256 == !scalarOpcode(opcode), std::string(name) + " has the wrong width");
    }
    const auto memory = memorySite(0xB9, false, false, 10, 4);
    const auto loaded = Codegen::DecodeFma(memory.data(), memory.size());
    require(loaded && std::string(loaded->Name) == "VFMADD231SS" && loaded->Memory && loaded->Destination == 10 && loaded->Second == 4, "a memory source was not decoded");
    for (const Bytes bytes : {site(0x95, false, false, 1, 2, 3), site(0xA5, false, false, 1, 2, 3), site(0x8C, false, false, 1, 2, 3), site(0xC8, false, false, 1, 2, 3),
             Bytes{0xC4, 0xE2, 0x50, 0xA8, 0xD9}, Bytes{0xC4, 0xE1, 0x51, 0xA8, 0xD9}, Bytes{0xC5, 0xD0, 0x59, 0xD9}, Bytes{0x90}})
        require(!Codegen::DecodeFma(bytes.data(), bytes.size()), "an instruction outside FMA3 was taken as FMA3");
    const Bytes trailing{0xC4, 0xE2, 0x51, 0xA8, 0xD9, 0x90};
    bool rejected = false;
    try { (void)Codegen::DecodeFma(trailing.data(), trailing.size()); }
    catch (const Codegen::CodegenException&) { rejected = true; }
    require(rejected, "trailing bytes were accepted");
}

void matching() {
    const auto avx = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Avx);
    const auto rosetta = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Rosetta);
    for (const auto opcode : opcodes()) {
        for (const bool wide : {false, true}) {
            for (const Bytes bytes : {site(opcode, wide, false, 3, 5, 1), memorySite(opcode, wide, true, 3, 5)}) {
                require(!rosetta->Match(bytes.data(), bytes.size()), "Rosetta mode lowered FMA3 opcode " + std::to_string(opcode));
                const auto match = avx->Match(bytes.data(), bytes.size());
                require(match && match->Lowering == Codegen::Amd64OnlyLowering::Trampoline && match->RelocatableWindow && std::string(match->InstructionName).rfind("VF", 0) == 0,
                    "AVX mode did not lower FMA3 opcode " + std::to_string(opcode) + " through a movable stub");
            }
        }
    }
    const Bytes multiply{0xC5, 0xD0, 0x59, 0xD9};
    require(!avx->Match(multiply.data(), multiply.size()), "AVX mode lowered VMULPS, which AVX has");
}

#if LOWERING_EXECUTION_AVAILABLE

using VectorExecution::Machine;
using VectorExecution::Runner;
using Vector = std::array<std::uint64_t, 4>;

constexpr std::int32_t kFlagsSlot = 520;

constexpr std::array<float, 16> kSingles{1.5f, -2.25f, 3.0e-20f, 1.0e20f, 0.0f, -0.0f, 16777217.0f, 1.00000012f, 0.1f, -0.3f, 1.0e-40f, 3.4e38f, -7.0f, 0.333333343f, 1.0e-30f, -1.0e25f};
constexpr std::array<double, 16> kDoubles{1.5, -2.25, 3.0e-200, 1.0e200, 0.0, -0.0, 9007199254740993.0, 1.0000000000000002, 0.1, -0.3, 4.9e-324, 1.7e308, -7.0, 0.3333333333333333, 1.0e-300, -1.0e250};

Vector values(bool wide, unsigned seed) {
    Vector vector{};
    if (wide) {
        std::array<double, 4> elements{};
        for (unsigned index = 0; index < 4; ++index) elements[index] = kDoubles[(seed * 7 + index * 5 + seed / 16) % kDoubles.size()];
        std::memcpy(vector.data(), elements.data(), sizeof(vector));
    } else {
        std::array<float, 8> elements{};
        for (unsigned index = 0; index < 8; ++index) elements[index] = kSingles[(seed * 7 + index * 5 + seed / 16) % kSingles.size()];
        std::memcpy(vector.data(), elements.data(), sizeof(vector));
    }
    return vector;
}

template <typename Element>
Element combine(std::uint8_t opcode, unsigned index, Element multiplicand, Element multiplier, Element addend) {
    const auto kind = opcode & 15;
    volatile double product = static_cast<double>(multiplicand) * static_cast<double>(multiplier);
    if (kind >= 12) product = -product;
    const bool subtract = kind == 6 ? (index & 1) == 0 : kind == 7 ? (index & 1) != 0 : (kind & 2) != 0;
    volatile double sum = subtract ? product - static_cast<double>(addend) : product + static_cast<double>(addend);
    return static_cast<Element>(sum);
}

template <typename Element>
Vector expected(std::uint8_t opcode, bool vector256, const Vector& first, const Vector& second, const Vector& third) {
    constexpr unsigned perVector = sizeof(Vector) / sizeof(Element);
    std::array<Element, perVector> a{}, b{}, c{}, result{};
    std::memcpy(a.data(), first.data(), sizeof(Vector));
    std::memcpy(b.data(), second.data(), sizeof(Vector));
    std::memcpy(c.data(), third.data(), sizeof(Vector));
    const bool scalar = scalarOpcode(opcode);
    const unsigned count = scalar ? 1 : vector256 ? perVector : perVector / 2;
    for (unsigned index = 0; index < perVector / 2; ++index) result[index] = scalar ? a[index] : Element{};
    for (unsigned index = 0; index < count; ++index) {
        switch (opcode >> 4) {
        case 0x9: result[index] = combine<Element>(opcode, index, a[index], c[index], b[index]); break;
        case 0xA: result[index] = combine<Element>(opcode, index, b[index], a[index], c[index]); break;
        default: result[index] = combine<Element>(opcode, index, b[index], c[index], a[index]); break;
        }
    }
    Vector vector{};
    std::memcpy(vector.data(), result.data(), sizeof(Vector));
    return vector;
}

Vector reference(std::uint8_t opcode, bool wide, bool vector256, const Vector& first, const Vector& second, const Vector& third) {
    return wide ? expected<double>(opcode, vector256, first, second, third) : expected<float>(opcode, vector256, first, second, third);
}

Bytes lowered(const Bytes& instruction) {
    Codegen::StubBodyBuilder body;
    Codegen::EmitFma(body, *Codegen::DecodeFma(instruction.data(), instruction.size()));
    const auto finished = body.Finish();
    return {finished.Bytes.begin(), finished.Bytes.begin() + static_cast<std::ptrdiff_t>(finished.ReturnBranchOffset)};
}

Bytes withFlags(const Bytes& stub, bool flagsSet) {
    Bytes code{0xB0, static_cast<std::uint8_t>(flagsSet ? 0x7F : 0x00), 0x04, 0x01, 0xB4, static_cast<std::uint8_t>(flagsSet ? 0xD5 : 0x00), 0x9E};
    code.insert(code.end(), stub.begin(), stub.end());
    code.insert(code.end(), {0x9F, 0x0F, 0x90, 0xC0, 0x0F, 0xB7, 0xC0, 0x48, 0x89, 0x87});
    for (unsigned shift = 0; shift < 32; shift += 8) code.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(kFlagsSlot) >> shift));
    return code;
}

std::string label(std::uint8_t opcode, bool wide, bool vector256) {
    const auto bytes = site(opcode, wide, vector256, 0, 0, 0);
    return std::string(Codegen::DecodeFma(bytes.data(), bytes.size())->Name) + (vector256 ? " ymm" : " xmm");
}

void verify(const Machine& start, const Machine& machine, std::uint8_t dst, const Vector& wanted, const std::string& describe) {
    require(machine.Ymm[dst] == wanted, describe + ": the result differs from multiply then add");
    for (std::uint8_t reg = 0; reg < 16; ++reg)
        if (reg != dst) require(machine.Ymm[reg] == start.Ymm[reg], describe + ": ymm" + std::to_string(reg) + " changed");
    for (const auto slot : machine.RedZone) require(slot == LoweringExecution::kSentinel, describe + " overwrote the red zone");
}

void execution() {
    const std::array<std::array<std::uint8_t, 3>, 10> triples{{{3, 5, 1}, {0, 0, 0}, {1, 1, 2}, {2, 3, 2}, {4, 5, 5}, {15, 14, 13}, {13, 15, 14}, {8, 12, 15}, {14, 2, 9}, {12, 13, 11}}};
    const std::array<std::array<std::uint8_t, 2>, 4> pairs{{{5, 6}, {15, 14}, {7, 7}, {13, 12}}};
    unsigned seed = 1;
    for (const auto opcode : opcodes()) {
        for (const bool wide : {false, true}) {
            for (const bool vector256 : {false, true}) {
                const auto describe = label(opcode, wide, vector256);
                for (const auto& triple : triples) {
                    const bool flagsSet = (seed & 1) != 0;
                    const Runner runner(withFlags(lowered(site(opcode, wide, vector256, triple[0], triple[1], triple[2])), flagsSet));
                    for (unsigned pattern = 0; pattern < 4; ++pattern) {
                        auto machine = VectorExecution::Initial();
                        for (const auto reg : triple) machine.Ymm[reg] = values(wide, seed++);
                        auto start = machine;
                        runner.Run(machine);
                        require(machine.Memory[1] == (flagsSet ? 0xD701u : 0x0200u), describe + ": RFLAGS changed");
                        machine.Memory[1] = start.Memory[1];
                        require(machine.Memory == start.Memory, describe + ": memory changed");
                        verify(start, machine, triple[0], reference(opcode, wide, vector256 && !scalarOpcode(opcode), start.Ymm[triple[0]], start.Ymm[triple[1]], start.Ymm[triple[2]]),
                            describe + " registers " + std::to_string(triple[0]) + "," + std::to_string(triple[1]) + "," + std::to_string(triple[2]) + " pattern " + std::to_string(pattern));
                    }
                }
                for (const auto& pair : pairs) {
                    const Runner runner(lowered(memorySite(opcode, wide, vector256, pair[0], pair[1])));
                    for (unsigned pattern = 0; pattern < 4; ++pattern) {
                        auto machine = VectorExecution::Initial();
                        machine.Memory = values(wide, seed++);
                        machine.Ymm[pair[1]] = values(wide, seed++);
                        if (pair[0] != pair[1]) machine.Ymm[pair[0]] = values(wide, seed++);
                        const auto start = machine;
                        runner.Run(machine);
                        require(machine.Memory == start.Memory, describe + ": the memory source changed");
                        verify(start, machine, pair[0], reference(opcode, wide, vector256 && !scalarOpcode(opcode), start.Ymm[pair[0]], start.Ymm[pair[1]], start.Memory),
                            describe + " registers " + std::to_string(pair[0]) + "," + std::to_string(pair[1]) + " and memory pattern " + std::to_string(pattern));
                    }
                }
            }
        }
    }
}

void pageEndMemory() {
    const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* area = static_cast<std::uint8_t*>(mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(area != MAP_FAILED, "cannot allocate the guarded operand pages");
    require(mprotect(area + pageSize, pageSize, PROT_NONE) == 0, "cannot protect the page after the operand");
    unsigned seed = 1000;
    for (const auto opcode : opcodes()) {
        for (const bool wide : {false, true}) {
            for (const bool vector256 : {false, true}) {
                const unsigned width = scalarOpcode(opcode) ? (wide ? 8 : 4) : vector256 ? 32 : 16;
                const auto memory = values(wide, seed++);
                auto* operand = area + pageSize - width;
                std::memcpy(operand, memory.data(), width);
                Bytes body{0x48, 0xB8};
                const auto address = reinterpret_cast<std::uintptr_t>(operand);
                for (unsigned shift = 0; shift < 64; shift += 8) body.push_back(static_cast<std::uint8_t>(address >> shift));
                const auto stub = lowered(memorySite(opcode, wide, vector256, 6, 9));
                body.insert(body.end(), stub.begin(), stub.end());
                auto machine = VectorExecution::Initial();
                machine.Ymm[6] = values(wide, seed++);
                machine.Ymm[9] = values(wide, seed++);
                const auto start = machine;
                Runner(body).Run(machine);
                verify(start, machine, 6, reference(opcode, wide, vector256 && !scalarOpcode(opcode), start.Ymm[6], start.Ymm[9], memory), label(opcode, wide, vector256) + " from memory at the end of a page");
            }
        }
    }
    munmap(area, pageSize * 2);
}

#else

void execution() { std::cout << "FMA execution checks require a Linux/macOS x86-64 build\n"; }
void pageEndMemory() {}

#endif

}

int main() {
    try {
        decoding();
        matching();
        execution();
        pageEndMemory();
        std::cout << "FMA lowering checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
