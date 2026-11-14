#include <codegen/x86/VexLegacyLowering.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include "VectorExecution.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <array>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string hex(const Bytes& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : bytes) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 15]);
    }
    return text;
}

const std::vector<std::pair<Bytes, Bytes>> kPairs{
    {Bytes{0xC5, 0xF1, 0xC4, 0xC9, 0x03}, Bytes{0x66, 0x0F, 0xC4, 0xC9, 0x03}},
    {Bytes{0xC5, 0xF9, 0xC5, 0xC2, 0x05}, Bytes{0x66, 0x0F, 0xC5, 0xC2, 0x05}},
    {Bytes{0xC4, 0xE3, 0x71, 0x02, 0xCA, 0x05}, Bytes{0x66, 0x0F, 0x3A, 0x0C, 0xCA, 0x05}},
    {Bytes{0xC5, 0xF0, 0x58, 0xCA}, Bytes{0x0F, 0x58, 0xCA}},
    {Bytes{0xC5, 0xE2, 0x58, 0x58, 0x10}, Bytes{0xF3, 0x0F, 0x58, 0x58, 0x10}},
    {Bytes{0xC4, 0x01, 0x1A, 0x59, 0x24, 0x91}, Bytes{0xF3, 0x47, 0x0F, 0x59, 0x24, 0x91}},
    {Bytes{0xC5, 0xFA, 0x10, 0x44, 0x24, 0x08}, Bytes{0xF3, 0x0F, 0x10, 0x44, 0x24, 0x08}},
    {Bytes{0xC5, 0xFA, 0x11, 0x4C, 0x24, 0x08}, Bytes{0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x08}},
    {Bytes{0xC5, 0xD2, 0x10, 0xEA}, Bytes{0xF3, 0x0F, 0x10, 0xEA}},
    {Bytes{0xC5, 0xF8, 0x57, 0xC0}, Bytes{0x0F, 0x57, 0xC0}},
    {Bytes{0xC4, 0x41, 0x30, 0x57, 0xC9}, Bytes{0x45, 0x0F, 0x57, 0xC9}},
    {Bytes{0xC5, 0xF8, 0x2E, 0xC1}, Bytes{0x0F, 0x2E, 0xC1}},
    {Bytes{0xC4, 0xE1, 0xFB, 0x2C, 0xC0}, Bytes{0xF2, 0x48, 0x0F, 0x2C, 0xC0}},
    {Bytes{0xC5, 0xFA, 0x2C, 0xCB}, Bytes{0xF3, 0x0F, 0x2C, 0xCB}},
    {Bytes{0xC5, 0xFA, 0x2A, 0xC0}, Bytes{0xF3, 0x0F, 0x2A, 0xC0}},
    {Bytes{0xC4, 0xE1, 0xDB, 0x2A, 0xE2}, Bytes{0xF2, 0x48, 0x0F, 0x2A, 0xE2}},
    {Bytes{0xC4, 0xE1, 0xF9, 0x6E, 0xC0}, Bytes{0x66, 0x48, 0x0F, 0x6E, 0xC0}},
    {Bytes{0xC5, 0xF9, 0x7E, 0xC9}, Bytes{0x66, 0x0F, 0x7E, 0xC9}},
    {Bytes{0xC4, 0xC1, 0xF9, 0x7E, 0xD0}, Bytes{0x66, 0x49, 0x0F, 0x7E, 0xD0}},
    {Bytes{0xC5, 0xF8, 0x10, 0x07}, Bytes{0x0F, 0x10, 0x07}},
    {Bytes{0xC5, 0x78, 0x11, 0x47, 0x20}, Bytes{0x44, 0x0F, 0x11, 0x47, 0x20}},
    {Bytes{0xC5, 0xF8, 0x28, 0xD1}, Bytes{0x0F, 0x28, 0xD1}},
    {Bytes{0xC5, 0xFA, 0x6F, 0x5E, 0x10}, Bytes{0xF3, 0x0F, 0x6F, 0x5E, 0x10}},
    {Bytes{0xC5, 0xF9, 0x62, 0xC1}, Bytes{0x66, 0x0F, 0x62, 0xC1}},
    {Bytes{0xC5, 0xF9, 0xC6, 0xC1, 0x01}, Bytes{0x66, 0x0F, 0xC6, 0xC1, 0x01}},
    {Bytes{0xC5, 0xF9, 0x70, 0xD1, 0x4E}, Bytes{0x66, 0x0F, 0x70, 0xD1, 0x4E}},
    {Bytes{0xC5, 0xE1, 0x73, 0xDB, 0x08}, Bytes{0x66, 0x0F, 0x73, 0xDB, 0x08}},
    {Bytes{0xC4, 0xC1, 0x29, 0x72, 0xF2, 0x04}, Bytes{0x66, 0x41, 0x0F, 0x72, 0xF2, 0x04}},
    {Bytes{0xC5, 0xF2, 0xC2, 0xCA, 0x01}, Bytes{0xF3, 0x0F, 0xC2, 0xCA, 0x01}},
    {Bytes{0xC5, 0xF8, 0x13, 0x07}, Bytes{0x0F, 0x13, 0x07}},
    {Bytes{0xC5, 0xF0, 0x16, 0x4E, 0x08}, Bytes{0x0F, 0x16, 0x4E, 0x08}},
    {Bytes{0xC5, 0xFA, 0x16, 0xD1}, Bytes{0xF3, 0x0F, 0x16, 0xD1}},
    {Bytes{0xC4, 0xE2, 0x79, 0x00, 0xC1}, Bytes{0x66, 0x0F, 0x38, 0x00, 0xC1}},
    {Bytes{0xC4, 0xE2, 0x79, 0x30, 0x08}, Bytes{0x66, 0x0F, 0x38, 0x30, 0x08}},
    {Bytes{0xC4, 0xE3, 0x79, 0x16, 0xC0, 0x01}, Bytes{0x66, 0x0F, 0x3A, 0x16, 0xC0, 0x01}},
    {Bytes{0xC4, 0xE3, 0x79, 0x22, 0xC1, 0x02}, Bytes{0x66, 0x0F, 0x3A, 0x22, 0xC1, 0x02}},
    {Bytes{0xC4, 0xE3, 0x79, 0x21, 0xC1, 0x10}, Bytes{0x66, 0x0F, 0x3A, 0x21, 0xC1, 0x10}},
    {Bytes{0xC4, 0xE3, 0x79, 0x0A, 0xC1, 0x09}, Bytes{0x66, 0x0F, 0x3A, 0x0A, 0xC1, 0x09}},
    {Bytes{0xC4, 0xE3, 0x79, 0x0C, 0xC1, 0x03}, Bytes{0x66, 0x0F, 0x3A, 0x0C, 0xC1, 0x03}},
    {Bytes{0xC5, 0xFB, 0x51, 0xC1}, Bytes{0xF2, 0x0F, 0x51, 0xC1}},
    {Bytes{0xC5, 0xFA, 0x5A, 0xC1}, Bytes{0xF3, 0x0F, 0x5A, 0xC1}},
    {Bytes{0xC5, 0xF8, 0x5B, 0xC1}, Bytes{0x0F, 0x5B, 0xC1}},
    {Bytes{0xC5, 0xF8, 0x50, 0xC1}, Bytes{0x0F, 0x50, 0xC1}},
    {Bytes{0xC5, 0xF9, 0xD7, 0xC1}, Bytes{0x66, 0x0F, 0xD7, 0xC1}},
    {Bytes{0xC4, 0xE2, 0x79, 0x17, 0xC1}, Bytes{0x66, 0x0F, 0x38, 0x17, 0xC1}},
    {Bytes{0x64, 0xC5, 0xFA, 0x10, 0x04, 0x25, 0x10, 0x00, 0x00, 0x00}, Bytes{0x64, 0xF3, 0x0F, 0x10, 0x04, 0x25, 0x10, 0x00, 0x00, 0x00}},
    {Bytes{0xC5, 0xFB, 0x10, 0x05, 0x23, 0x01, 0x00, 0x00}, Bytes{0xF2, 0x0F, 0x10, 0x05, 0x23, 0x01, 0x00, 0x00}},
    {Bytes{0xC5, 0xF8, 0x10, 0x05, 0x10, 0x00, 0x00, 0x00}, Bytes{0x0F, 0x10, 0x05, 0x11, 0x00, 0x00, 0x00}},
};

const std::vector<Bytes> kKept{
    Bytes{0xC4, 0xE3, 0xF9, 0x16, 0xC0, 0x01},
    Bytes{0xC5, 0xF0, 0x58, 0x08},
    Bytes{0xC5, 0xE0, 0x58, 0xCA},
    Bytes{0xC5, 0xF4, 0x58, 0xCA},
    Bytes{0xC5, 0xF0, 0xC2, 0xCA, 0x1F},
    Bytes{0xC4, 0xE2, 0x71, 0xA9, 0xC2},
    Bytes{0xC4, 0xE2, 0x79, 0x18, 0x00},
    Bytes{0xC5, 0xE1, 0x72, 0xD1, 0x02},
    Bytes{0xC5, 0xD2, 0x10, 0xF2},
    Bytes{0xC4, 0xE3, 0x79, 0x04, 0xC1, 0x1B},
    Bytes{0xC5, 0x7A, 0x10, 0x0D, 0x40, 0x00, 0x00, 0x00},
};

const std::vector<Bytes> kStubbed{
    Bytes{0xC5, 0xE1, 0xC4, 0xC9, 0x01},
    Bytes{0xC4, 0xE3, 0x61, 0x02, 0x08, 0x09},
    Bytes{0xC5, 0xE0, 0x58, 0xCA},
    Bytes{0xC5, 0xE0, 0x5C, 0xC9},
    Bytes{0xC5, 0xE1, 0xFE, 0xC9},
    Bytes{0xC5, 0xF0, 0x58, 0x08},
    Bytes{0xC5, 0x60, 0x59, 0x08},
    Bytes{0xC5, 0xE2, 0x5C, 0xCA},
    Bytes{0xC5, 0xE2, 0x5C, 0xC9},
    Bytes{0xC5, 0xE2, 0x58, 0x08},
    Bytes{0xC5, 0xD1, 0x72, 0xD2, 0x03},
    Bytes{0xC5, 0xE2, 0x10, 0xCA},
    Bytes{0xC5, 0xE2, 0x2A, 0xC9},
    Bytes{0xC5, 0x7A, 0x10, 0x48, 0x10},
    Bytes{0xC5, 0xE0, 0xC6, 0xCA, 0x1B},
    Bytes{0xC5, 0xF9, 0x70, 0x20, 0x1B},
    Bytes{0xC5, 0x99, 0x60, 0xE4},
    Bytes{0xC4, 0x41, 0x23, 0x5E, 0xC0},
    Bytes{0xC4, 0xE2, 0x41, 0x39, 0xF6},
    Bytes{0xC5, 0xE0, 0x16, 0x48, 0x08},
    Bytes{0xC5, 0xE8, 0x55, 0xC9},
    Bytes{0xC4, 0xC1, 0x69, 0x73, 0xF6, 0x07},
    Bytes{0xC5, 0xE2, 0x11, 0xCA},
};

void shadowLayout() {
    require(Codegen::UpperShadowReservation(1) == 256 && Codegen::UpperShadowReservation(0) == 256 && Codegen::UpperShadowReservation(512) == 512, "the upper shadow reservation must be 256 bytes rounded to the TLS alignment");
    require(Codegen::UpperShadowOffset(0, 1) == -256, "an empty TLS block puts the shadow at fs:-256");
    require(Codegen::UpperShadowOffset(0xF0, 8) == -0x1F0, "the shadow must sit before the guest's 0xF0-byte TLS block");
    require(Codegen::UpperShadowOffset(0x150, 16) == -0x250, "the shadow must sit before the guest's 0x150-byte TLS block");
    require(Codegen::UpperShadowOffset(0x151, 16) == -0x260, "the guest block rounds up to its alignment before the shadow");
}

const std::vector<Bytes> kShadowed{
    Bytes{0xC4, 0xE2, 0x69, 0x0C, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0x0C, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0x0C, 0x08},
    Bytes{0xC4, 0xE2, 0x69, 0x0D, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0x0D, 0xD2},
    Bytes{0xC4, 0x42, 0x2D, 0x0D, 0xC3},
    Bytes{0xC4, 0xE2, 0x79, 0x78, 0xCA},
    Bytes{0xC4, 0xE2, 0x7D, 0x78, 0xC9},
    Bytes{0xC4, 0xE2, 0x7D, 0x78, 0x48, 0x05},
    Bytes{0xC4, 0xE2, 0x79, 0x79, 0xCA},
    Bytes{0xC4, 0x42, 0x7D, 0x79, 0xC1},
    Bytes{0xC4, 0xE2, 0x79, 0x79, 0x48, 0x06},
    Bytes{0xC5, 0xFD, 0x5A, 0xCA},
    Bytes{0xC5, 0xFD, 0x5A, 0x08},
    Bytes{0xC5, 0xFD, 0xE6, 0xD2},
    Bytes{0xC5, 0xFF, 0xE6, 0xCA},
    Bytes{0xC4, 0x41, 0x7D, 0xE6, 0xC1},
    Bytes{0xC5, 0xFC, 0x5A, 0xCA},
    Bytes{0xC5, 0xFC, 0x5A, 0x48, 0x08},
    Bytes{0xC5, 0xFE, 0xE6, 0xD2},
    Bytes{0xC5, 0xFE, 0xE6, 0x08},
    Bytes{0xC4, 0xE2, 0x69, 0x47, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0x47, 0xCB},
    Bytes{0xC4, 0xE2, 0xED, 0x47, 0x08},
    Bytes{0xC4, 0xE2, 0x6D, 0x45, 0xD2},
    Bytes{0xC4, 0xE2, 0xE9, 0x45, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0x46, 0xCB},
    Bytes{0xC4, 0x42, 0x29, 0x46, 0xC3},
    Bytes{0xC4, 0xE2, 0x79, 0x13, 0xCA},
    Bytes{0xC4, 0x42, 0x79, 0x13, 0xC0},
    Bytes{0xC4, 0xE3, 0x79, 0x1D, 0xD1, 0x00},
    Bytes{0xC4, 0xE3, 0x79, 0x1D, 0xD1, 0x03},
    Bytes{0xC4, 0xE3, 0x79, 0x1D, 0xC0, 0x00},
    Bytes{0xC4, 0xE2, 0x7D, 0x30, 0xCA},
    Bytes{0xC4, 0xE2, 0x7D, 0x31, 0xD2},
    Bytes{0xC4, 0xE2, 0x7D, 0x32, 0x18},
    Bytes{0xC4, 0xE2, 0x7D, 0x23, 0xCA},
    Bytes{0xC4, 0x42, 0x7D, 0x24, 0xC1},
    Bytes{0xC4, 0xE2, 0x7D, 0x25, 0x20},
    Bytes{0xC4, 0xE2, 0x7D, 0x35, 0xCA},
    Bytes{0xC4, 0xE2, 0x7D, 0x20, 0x48, 0x04},
    Bytes{0xC4, 0xE3, 0x69, 0x4A, 0xCB, 0x40},
    Bytes{0xC4, 0xE3, 0x6D, 0x4A, 0xCB, 0x40},
    Bytes{0xC4, 0xE3, 0x55, 0x4B, 0x28, 0x00},
    Bytes{0xC4, 0xE3, 0x75, 0x4C, 0xC2, 0x00},
    Bytes{0xC4, 0x43, 0x29, 0x4C, 0xCB, 0xC0},
    Bytes{0xC4, 0xE3, 0xFD, 0x00, 0xCA, 0x4E},
    Bytes{0xC4, 0xE3, 0xFD, 0x00, 0xD2, 0xD8},
    Bytes{0xC4, 0xE3, 0xFD, 0x01, 0x18, 0x93},
    Bytes{0xC4, 0xE2, 0x6D, 0x36, 0xCB},
    Bytes{0xC4, 0xE2, 0x55, 0x16, 0x20},
    Bytes{0xC4, 0xE2, 0x6D, 0x36, 0xD2},
    Bytes{0xC4, 0xE3, 0x7D, 0x04, 0xCA, 0x1B},
    Bytes{0xC4, 0xE3, 0x7D, 0x05, 0x18, 0x05},
    Bytes{0xC4, 0xE3, 0x79, 0x04, 0xC9, 0x93},
    Bytes{0xC4, 0x43, 0x79, 0x05, 0xC1, 0x01},
    Bytes{0xC4, 0xE3, 0x6D, 0x02, 0xD9, 0xA5},
    Bytes{0xC4, 0xE3, 0x6D, 0x0D, 0xD9, 0x09},
    Bytes{0xC5, 0xED, 0xC6, 0xD9, 0x05},
    Bytes{0xC4, 0xE3, 0x6D, 0x42, 0xD9, 0x2D},
    Bytes{0xC4, 0xE3, 0x6D, 0x0E, 0xD9, 0x96},
    Bytes{0xC5, 0xE4, 0x58, 0xCA},
    Bytes{0xC5, 0xE4, 0x5C, 0xC9},
    Bytes{0xC5, 0xDC, 0x57, 0xE4},
    Bytes{0xC5, 0xFC, 0x10, 0x28},
    Bytes{0xC5, 0xFC, 0x11, 0x30},
    Bytes{0xC5, 0x7C, 0x28, 0xCA},
    Bytes{0xC4, 0xE3, 0x65, 0x18, 0xCA, 0x01},
    Bytes{0xC4, 0xE3, 0x65, 0x18, 0xCA, 0x00},
    Bytes{0xC4, 0xE3, 0x65, 0x18, 0x18, 0x01},
    Bytes{0xC4, 0xE3, 0x7D, 0x19, 0xD9, 0x01},
    Bytes{0xC4, 0xE3, 0x7D, 0x19, 0x18, 0x01},
    Bytes{0xC4, 0xE3, 0x7D, 0x19, 0xDF, 0x00},
    Bytes{0xC4, 0xE3, 0x65, 0x06, 0xCA, 0x21},
    Bytes{0xC4, 0xE3, 0x65, 0x06, 0x08, 0x83},
    Bytes{0xC4, 0xE2, 0x7D, 0x18, 0x08},
    Bytes{0xC4, 0xE2, 0x7D, 0x18, 0xCA},
    Bytes{0xC4, 0xE2, 0x7D, 0x19, 0x60, 0x08},
    Bytes{0xC4, 0x62, 0x7D, 0x59, 0xE3},
    Bytes{0xC4, 0xE2, 0x79, 0x58, 0x08},
    Bytes{0xC4, 0xE2, 0x7D, 0x1A, 0x10},
    Bytes{0xC5, 0xD5, 0x72, 0xD2, 0x03},
    Bytes{0xC5, 0xED, 0xF3, 0xD9},
    Bytes{0xC4, 0xE2, 0x6D, 0x00, 0xD9},
    Bytes{0xC4, 0xE3, 0x6D, 0x0C, 0xD9, 0x5A},
    Bytes{0xC5, 0xED, 0xFE, 0x10},
    Bytes{0xC5, 0xF8, 0x77},
    Bytes{0xC5, 0xFC, 0x77},
    Bytes{0xC5, 0xE0, 0x58, 0xCA},
    Bytes{0xC5, 0xD8, 0x57, 0xE4},
    Bytes{0xC5, 0x7A, 0x10, 0x08},
    Bytes{0xC5, 0xED, 0x74, 0xD9},
    Bytes{0xC5, 0xEC, 0x55, 0xC9},
    Bytes{0xC5, 0xE4, 0xC6, 0xCA, 0x1B},
    Bytes{0xC5, 0xFE, 0x7F, 0x18},
};

const std::vector<Bytes> kFma{
    Bytes{0xC4, 0xE2, 0x71, 0xA9, 0xC3},
    Bytes{0xC4, 0xE2, 0x61, 0xB9, 0x00},
    Bytes{0xC4, 0xE2, 0x79, 0xBB, 0x08},
    Bytes{0xC4, 0xE2, 0x79, 0xBF, 0x48, 0x04},
    Bytes{0xC4, 0xE2, 0x69, 0xBD, 0xC1},
    Bytes{0xC4, 0xE2, 0x71, 0x99, 0xC2},
    Bytes{0xC4, 0xE2, 0x51, 0xA8, 0xD9},
    Bytes{0xC4, 0x42, 0x41, 0xB8, 0xCA},
    Bytes{0xC4, 0x42, 0x2D, 0xB8, 0xCB},
    Bytes{0xC4, 0xE2, 0x6D, 0xA8, 0x08},
    Bytes{0xC4, 0xE2, 0xF1, 0x99, 0xC2},
    Bytes{0xC4, 0xE2, 0xE9, 0xAC, 0x08},
    Bytes{0xC4, 0xE2, 0x71, 0xA6, 0xC2},
    Bytes{0xC4, 0xE2, 0xF5, 0xB7, 0xC2},
    Bytes{0xC4, 0xE2, 0x65, 0xAA, 0xDB},
};

void rewriting() {
    for (const auto& [vex, legacy] : kPairs) {
        const auto decoded = Codegen::DecodeVexLegacy(vex.data(), vex.size());
        require(decoded && decoded->Replacement == legacy, "VEX " + hex(vex) + " became " + (decoded ? hex(decoded->Replacement) : std::string("nothing")) + ", wanted " + hex(legacy));
    }
    for (const auto& vex : kKept)
        require(!Codegen::DecodeVexLegacy(vex.data(), vex.size()), "VEX " + hex(vex) + " has no same-length legacy form but was rewritten");
    const Bytes zeroUpper{0xC5, 0xF8, 0x77};
    const auto cleared = Codegen::DecodeVexLegacy(zeroUpper.data(), zeroUpper.size());
    require(cleared && cleared->Replacement.empty(), "VZEROUPPER must become padding");
    const Bytes zeroAll{0xC5, 0xFC, 0x77};
    require(!Codegen::DecodeVexLegacy(zeroAll.data(), zeroAll.size()), "VZEROALL was rewritten");
}

void matching() {
    const auto sse = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Sse);
    const auto intel = Codegen::MakeAmd64OnlyInstructionMatcher();
    for (const auto& [vex, legacy] : kPairs) {
        const auto match = sse->Match(vex.data(), vex.size());
        require(match && match->Lowering == Codegen::Amd64OnlyLowering::InPlace && match->ReplacementBytes.size() == vex.size(), "SSE mode did not rewrite " + hex(vex) + " in place");
        require(Bytes(match->ReplacementBytes.begin(), match->ReplacementBytes.begin() + static_cast<std::ptrdiff_t>(legacy.size())) == legacy, "SSE mode changed the legacy bytes of " + hex(vex));
        require(!intel->Match(vex.data(), vex.size()), "Intel mode rewrote " + hex(vex));
    }
    const Bytes zeroUpper{0xC5, 0xF8, 0x77};
    const auto cleared = sse->Match(zeroUpper.data(), zeroUpper.size());
    require(cleared && cleared->InstructionName == "VZEROUPPER" && cleared->ReplacementBytes == Bytes{0x0F, 0x1F, 0x00}, "VZEROUPPER did not become a three-byte NOP");
}

void stubbing() {
    const auto sse = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Sse);
    for (const auto& vex : kStubbed) {
        require(!Codegen::DecodeVexLegacy(vex.data(), vex.size()), "VEX " + hex(vex) + " was rewritten in place");
        require(Codegen::DecodeVexStub(vex.data(), vex.size()).has_value(), "VEX " + hex(vex) + " has no stub");
        const auto match = sse->Match(vex.data(), vex.size());
        require(match && match->Lowering == Codegen::Amd64OnlyLowering::Trampoline, "SSE mode did not move " + hex(vex) + " to a stub");
    }
}

#if LOWERING_EXECUTION_AVAILABLE

Bytes lowered(const Bytes& vex) {
    Codegen::StubBodyBuilder body;
    Codegen::EmitVexStub(body, *Codegen::DecodeVexStub(vex.data(), vex.size()));
    const auto finished = body.Finish();
    require(finished.Relocations.empty(), "test stubs must not relocate");
    return Bytes(finished.Bytes.begin(), finished.Bytes.begin() + static_cast<std::ptrdiff_t>(finished.ReturnBranchOffset));
}

std::uint8_t destination(const Bytes& vex) {
    const bool threeByte = vex[0] == 0xC4;
    const auto r = static_cast<std::uint8_t>((vex[1] & 0x80) ? 0 : 8);
    const auto op = vex[threeByte ? 3 : 2];
    const auto modrm = vex[threeByte ? 4 : 3];
    const auto map = threeByte ? (vex[1] & 0x1F) : 1;
    const auto last = threeByte ? vex[2] : vex[1];
    if (map == 1 && op == 0x11) return static_cast<std::uint8_t>((threeByte && !(vex[1] & 0x20) ? 8 : 0) | (modrm & 7));
    if (map == 1 && op >= 0x71 && op <= 0x73) return static_cast<std::uint8_t>(~last >> 3 & 0xF);
    return static_cast<std::uint8_t>(r | (modrm >> 3 & 7));
}

void execution() {
    for (const auto& vex : kStubbed) {
        const VectorExecution::Runner reference(vex);
        const VectorExecution::Runner stub(lowered(vex));
        for (int seed = 0; seed < 4; ++seed) {
            auto start = VectorExecution::Initial();
            for (auto& reg : start.Ymm)
                for (auto& q : reg) q = q * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(seed) * 0x3F8000003F800000ull;
            for (auto& q : start.Memory) q ^= static_cast<std::uint64_t>(seed) * 0x4049000040490000ull;
            auto wanted = start;
            auto got = start;
            reference.Run(wanted);
            stub.Run(got);
            const auto dst = destination(vex);
            for (std::uint8_t reg = 0; reg < 16; ++reg)
                for (std::size_t q = 0; q < (reg == dst ? 2u : 4u); ++q)
                    require(got.Ymm[reg][q] == wanted.Ymm[reg][q], "stub of " + hex(vex) + " differs in register " + std::to_string(reg) + " quadword " + std::to_string(q));
            require(got.Memory == wanted.Memory, "stub of " + hex(vex) + " changed memory");
            for (const auto red : got.RedZone) require(red == LoweringExecution::kSentinel, "stub of " + hex(vex) + " overwrote the red zone");
        }
    }
}

alignas(16) thread_local std::uint64_t shadowArea[32];

std::int32_t shadowOffset() {
    std::uint64_t threadPointer = 0;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(threadPointer));
    return static_cast<std::int32_t>(reinterpret_cast<std::uint64_t>(&shadowArea[0]) - threadPointer);
}

template <typename Finished>
Bytes pooled(const Finished& finished) {
    constexpr std::size_t kRunnerPrologue = 14 + 7 + 16 * 9 + 16 * 15;
    Bytes code((16 - kRunnerPrologue % 16) % 16, 0x90);
    const auto start = code.size();
    code.insert(code.end(), finished.Bytes.begin(), finished.Bytes.end());
    const auto skip = static_cast<std::int32_t>(finished.Bytes.size() - finished.ReturnBranchOffset - 5);
    std::memcpy(code.data() + start + finished.ReturnBranchOffset + 1, &skip, sizeof(skip));
    return code;
}

void shadowExecution() {
    const auto offset = shadowOffset();
    for (const auto& vex : kShadowed) {
        const auto site = Codegen::DecodeVexShadow(vex.data(), vex.size(), true);
        require(site.has_value(), "VEX " + hex(vex) + " has no shadow lowering");
        Codegen::StubBodyBuilder body;
        Codegen::EmitVexShadow(body, *site, offset);
        const auto finished = body.Finish();
        require(finished.Relocations.empty(), "test stubs must not relocate");
        const VectorExecution::Runner reference(vex);
        const VectorExecution::Runner stub(pooled(finished));
        for (int seed = 0; seed < 4; ++seed) {
            auto start = VectorExecution::Initial();
            for (auto& reg : start.Ymm)
                for (auto& q : reg) q = q * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(seed) * 0x3F8000003F800000ull;
            for (auto& q : start.Memory) q ^= static_cast<std::uint64_t>(seed) * 0x4049000040490000ull;
            for (std::size_t reg = 0; reg < 16; ++reg) {
                shadowArea[reg * 2] = start.Ymm[reg][2];
                shadowArea[reg * 2 + 1] = start.Ymm[reg][3];
            }
            auto wanted = start;
            auto got = start;
            reference.Run(wanted);
            stub.Run(got);
            for (std::size_t reg = 0; reg < 16; ++reg) {
                require(got.Ymm[reg][0] == wanted.Ymm[reg][0] && got.Ymm[reg][1] == wanted.Ymm[reg][1], "shadow stub of " + hex(vex) + " differs in the low half of register " + std::to_string(reg));
                require(shadowArea[reg * 2] == wanted.Ymm[reg][2] && shadowArea[reg * 2 + 1] == wanted.Ymm[reg][3], "shadow stub of " + hex(vex) + " differs in the upper half of register " + std::to_string(reg));
            }
            require(got.Memory == wanted.Memory, "shadow stub of " + hex(vex) + " changed memory");
            for (const auto red : got.RedZone) require(red == LoweringExecution::kSentinel, "shadow stub of " + hex(vex) + " overwrote the red zone");
        }
    }
}

void halfExecution() {
    const auto offset = shadowOffset();
    const auto compile = [&](const Bytes& vex) {
        const auto site = Codegen::DecodeVexShadow(vex.data(), vex.size(), true);
        require(site.has_value(), "VEX " + hex(vex) + " has no shadow lowering");
        Codegen::StubBodyBuilder body;
        Codegen::EmitVexShadow(body, *site, offset);
        return pooled(body.Finish());
    };
    const auto compare = [&](const Bytes& vex, const VectorExecution::Runner& reference, const VectorExecution::Runner& stub, const std::uint64_t low, const std::uint64_t high) {
        auto start = VectorExecution::Initial();
        start.Ymm[2][0] = low;
        start.Ymm[2][1] = high;
        auto wanted = start;
        auto got = start;
        reference.Run(wanted);
        stub.Run(got);
        require(got.Ymm[1][0] == wanted.Ymm[1][0] && got.Ymm[1][1] == wanted.Ymm[1][1], "half conversion " + hex(vex) + " differs for input " + std::to_string(low) + ":" + std::to_string(high));
    };
    const Bytes widen{0xC4, 0xE2, 0x79, 0x13, 0xCA};
    const VectorExecution::Runner widenReference(widen);
    const VectorExecution::Runner widenStub(compile(widen));
    for (std::uint64_t half = 0; half < 0x10000; half += 4)
        compare(widen, widenReference, widenStub, half | (half + 1) << 16 | (half + 2) << 32 | (half + 3) << 48, 0);
    std::uint64_t state = 0x2545F4914F6CDD1Dull;
    for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{3}}) {
        const Bytes narrow{0xC4, 0xE3, 0x79, 0x1D, 0xD1, mode};
        const VectorExecution::Runner narrowReference(narrow);
        const VectorExecution::Runner narrowStub(compile(narrow));
        for (int round = 0; round < 16384; ++round) {
            std::uint64_t words[2]{};
            for (auto& word : words) {
                std::uint64_t pair = 0;
                for (int lane = 0; lane < 2; ++lane) {
                    state = state * 6364136223846793005ull + 1442695040888963407ull;
                    const auto bits = static_cast<std::uint32_t>(state >> 32);
                    const auto exponent = 98u + (bits >> 8) % 60u;
                    auto single = (bits & 0x80000000u) | exponent << 23 | (static_cast<std::uint32_t>(state >> 9) & 0x7FFFFFu);
                    if ((bits & 7u) == 0) single = (single & ~0x1FFFu) | 0x1000u;
                    if ((bits & 63u) == 1) single = (bits & 0x80000000u) | 0x7F800000u | (bits & 0x10u ? (bits >> 10 & 0x7FFFFFu) | 1u : 0u);
                    pair |= static_cast<std::uint64_t>(single) << (32 * lane);
                }
                word = pair;
            }
            compare(narrow, narrowReference, narrowStub, words[0], words[1]);
        }
    }
}

void shiftExecution() {
    const auto offset = shadowOffset();
    const std::vector<Bytes> shifts{
        Bytes{0xC4, 0xE2, 0x6D, 0x47, 0xCB}, Bytes{0xC4, 0xE2, 0xED, 0x47, 0xCB}, Bytes{0xC4, 0xE2, 0x6D, 0x45, 0xCB},
        Bytes{0xC4, 0xE2, 0xED, 0x45, 0xCB}, Bytes{0xC4, 0xE2, 0x6D, 0x46, 0xCB}, Bytes{0xC4, 0xE2, 0x69, 0x46, 0xCB},
    };
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state;
    };
    for (const auto& vex : shifts) {
        const auto site = Codegen::DecodeVexShadow(vex.data(), vex.size(), true);
        require(site.has_value(), "VEX " + hex(vex) + " has no shadow lowering");
        Codegen::StubBodyBuilder body;
        Codegen::EmitVexShadow(body, *site, offset);
        const VectorExecution::Runner reference(vex);
        const VectorExecution::Runner stub(pooled(body.Finish()));
        for (int round = 0; round < 256; ++round) {
            auto start = VectorExecution::Initial();
            for (auto& q : start.Ymm[2]) q = next();
            for (auto& q : start.Ymm[3]) {
                const auto pick = next();
                q = (pick >> 8) % 70 | ((pick >> 24) % 70) << 32;
                if ((pick & 15) == 0) q |= 0x100000000ull << (pick >> 4 & 15);
            }
            for (std::size_t reg = 0; reg < 16; ++reg) {
                shadowArea[reg * 2] = start.Ymm[reg][2];
                shadowArea[reg * 2 + 1] = start.Ymm[reg][3];
            }
            auto wanted = start;
            auto got = start;
            reference.Run(wanted);
            stub.Run(got);
            require(got.Ymm[1][0] == wanted.Ymm[1][0] && got.Ymm[1][1] == wanted.Ymm[1][1] && shadowArea[2] == wanted.Ymm[1][2] && shadowArea[3] == wanted.Ymm[1][3], "variable shift " + hex(vex) + " differs in round " + std::to_string(round));
        }
    }
}

void flagsExecution() {
    const auto offset = shadowOffset();
    const Bytes saveFlags{0x9C, 0x59, 0x66, 0x4C, 0x0F, 0x6E, 0xF9};
    const std::vector<Bytes> tests{
        Bytes{0xC4, 0xE2, 0x7D, 0x17, 0xCA}, Bytes{0xC4, 0xE2, 0x7D, 0x17, 0x08}, Bytes{0xC4, 0xE2, 0x79, 0x0E, 0xCA},
        Bytes{0xC4, 0xE2, 0x7D, 0x0E, 0xCA}, Bytes{0xC4, 0xE2, 0x79, 0x0F, 0xCA}, Bytes{0xC4, 0xE2, 0x7D, 0x0F, 0x08},
        Bytes{0xC4, 0x42, 0x7D, 0x17, 0xC1},
    };
    std::uint64_t state = 0xD1B54A32D192ED03ull;
    const auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state;
    };
    for (const auto& vex : tests) {
        const auto site = Codegen::DecodeVexShadow(vex.data(), vex.size(), true);
        require(site.has_value(), "VEX " + hex(vex) + " has no shadow lowering");
        Codegen::StubBodyBuilder body;
        Codegen::EmitVexShadow(body, *site, offset);
        auto lowered = pooled(body.Finish());
        lowered.insert(lowered.end(), saveFlags.begin(), saveFlags.end());
        auto native = vex;
        native.insert(native.end(), saveFlags.begin(), saveFlags.end());
        const VectorExecution::Runner reference(native);
        const VectorExecution::Runner stub(lowered);
        for (int round = 0; round < 200; ++round) {
            auto start = VectorExecution::Initial();
            const auto first = vex[1] == 0x42 ? 8 : 1;
            const auto second = vex[1] == 0x42 ? 9 : 2;
            for (std::size_t q = 0; q < 4; ++q) {
                const auto a = next();
                const auto r = next();
                const auto shape = (round + static_cast<int>(q)) % 6;
                const auto b = shape == 0 ? 0 : shape == 1 ? a : shape == 2 ? ~a : shape == 3 ? (a & r) : shape == 4 ? (~a & r & 0x7FFFFFFF7FFFFFFFull) : r;
                start.Ymm[first][q] = round % 7 == 0 ? 0 : a;
                start.Ymm[second][q] = b;
                start.Memory[q] = b;
            }
            for (std::size_t reg = 0; reg < 16; ++reg) {
                shadowArea[reg * 2] = start.Ymm[reg][2];
                shadowArea[reg * 2 + 1] = start.Ymm[reg][3];
            }
            auto wanted = start;
            auto got = start;
            reference.Run(wanted);
            stub.Run(got);
            require((got.Ymm[15][0] & 0x8D5) == (wanted.Ymm[15][0] & 0x8D5), "test " + hex(vex) + " sets different flags in round " + std::to_string(round));
            for (std::size_t reg = 0; reg < 15; ++reg)
                require(got.Ymm[reg][0] == wanted.Ymm[reg][0] && got.Ymm[reg][1] == wanted.Ymm[reg][1], "test " + hex(vex) + " changed register " + std::to_string(reg));
        }
    }
}

void fmaExecution() {
    const auto offset = shadowOffset();
    for (const auto& vex : kFma) {
        const auto fma = Codegen::DecodeFmaSse(vex.data(), vex.size(), true);
        require(fma.has_value(), "VEX " + hex(vex) + " has no FMA lowering");
        Codegen::StubBodyBuilder body;
        Codegen::EmitFmaSse(body, *fma, offset);
        const auto finished = body.Finish();
        const VectorExecution::Runner reference(vex);
        require(finished.Relocations.empty(), "test stubs must not relocate");
        const VectorExecution::Runner stub(pooled(finished));
        const bool doubles = fma->Double;
        for (int seed = 0; seed < 4; ++seed) {
            auto start = VectorExecution::Initial();
            std::uint64_t state = 0x9E3779B97F4A7C15ull * static_cast<std::uint64_t>(seed + 1);
            const auto next = [&](const double low, const double high) {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                return low + (high - low) * static_cast<double>(state >> 11) / 9007199254740992.0;
            };
            const auto fill = [&](std::uint64_t* words, const std::size_t count, const double low, const double high) {
                for (std::size_t q = 0; q < count; ++q) {
                    if (doubles) {
                        const double value = next(low, high);
                        std::memcpy(&words[q], &value, sizeof(value));
                    } else {
                        const float pair[2]{static_cast<float>(next(low, high)), static_cast<float>(next(low, high))};
                        std::memcpy(&words[q], pair, sizeof(pair));
                    }
                }
            };
            for (auto& reg : start.Ymm) fill(reg.data(), 4, 1.5, 2.0);
            fill(start.Memory.data(), 4, 0.25, 0.5);
            for (std::size_t reg = 0; reg < 16; ++reg) {
                shadowArea[reg * 2] = start.Ymm[reg][2];
                shadowArea[reg * 2 + 1] = start.Ymm[reg][3];
            }
            auto wanted = start;
            auto got = start;
            reference.Run(wanted);
            stub.Run(got);
            for (std::size_t reg = 0; reg < 16; ++reg) {
                const std::array<std::uint64_t, 4> lowered{got.Ymm[reg][0], got.Ymm[reg][1], shadowArea[reg * 2], shadowArea[reg * 2 + 1]};
                for (std::size_t q = 0; q < 4; ++q) {
                    if (lowered[q] == wanted.Ymm[reg][q]) continue;
                    bool close = reg == fma->Destination;
                    if (close && doubles) {
                        double x = 0, y = 0;
                        std::memcpy(&x, &lowered[q], sizeof(x));
                        std::memcpy(&y, &wanted.Ymm[reg][q], sizeof(y));
                        close = std::fabs(x - y) <= 1e-13;
                    } else if (close) {
                        float x[2], y[2];
                        std::memcpy(x, &lowered[q], sizeof(x));
                        std::memcpy(y, &wanted.Ymm[reg][q], sizeof(y));
                        close = std::fabs(x[0] - y[0]) <= 1e-5f && std::fabs(x[1] - y[1]) <= 1e-5f;
                    }
                    require(close, "FMA stub of " + hex(vex) + " differs in register " + std::to_string(reg) + " quadword " + std::to_string(q));
                }
            }
            require(got.Memory == wanted.Memory, "FMA stub of " + hex(vex) + " changed memory");
        }
    }
}

#else

void halfExecution() {}
void shiftExecution() {}
void flagsExecution() {}
void fmaExecution() { std::cout << "FMA execution checks require a Linux/macOS x86-64 build\n"; }
void execution() { std::cout << "VEX.128 stub execution checks require a Linux/macOS x86-64 build\n"; }
void shadowExecution() { std::cout << "VEX shadow execution checks require a Linux/macOS x86-64 build\n"; }

#endif

void outOfLine() {
    for (const auto& [vex, legacy] : kPairs) {
        Codegen::StubBodyBuilder body;
        Codegen::EmitVexLegacy(body, *Codegen::DecodeVexLegacy(vex.data(), vex.size()));
        const auto lowered = body.Finish();
        if (lowered.Relocations.empty())
            require(lowered.Bytes.size() >= legacy.size() && Bytes(lowered.Bytes.begin(), lowered.Bytes.begin() + static_cast<std::ptrdiff_t>(legacy.size())) == legacy, "The stub of " + hex(vex) + " does not start with its legacy form");
        else
            require(lowered.Relocations.size() == 1, "The stub of " + hex(vex) + " must relocate its one RIP-relative operand");
    }
}

}

int main() {
    try {
        shadowLayout();
        rewriting();
        matching();
        outOfLine();
        stubbing();
        execution();
        shadowExecution();
        halfExecution();
        shiftExecution();
        flagsExecution();
        fmaExecution();
        std::cout << "VEX.128 legacy lowering checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
