#include "prx/libSceFont/include/FontTypes.hpp"
#include "prx/libSceFont/include/FontDriver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

extern "C" {
int APS5_VABI sceFontMemoryInit(FontMemory*, void*, std::uint32_t, const FontMemoryInterface*, void*, FontMemoryDestroyFunction, void*);
int APS5_VABI sceFontMemoryTerm(FontMemory*);
int APS5_VABI sceFontCreateLibrary(const FontMemory*, const void*, FontLibrary*);
int APS5_VABI sceFontDestroyLibrary(FontLibrary*);
int APS5_VABI sceFontCreateRenderer(const FontMemory*, const void*, FontRenderer*);
int APS5_VABI sceFontDestroyRenderer(FontRenderer*);
int APS5_VABI sceFontSupportExternalFonts(FontLibrary, std::uint32_t, std::uint32_t);
int APS5_VABI sceFontOpenFontMemory(FontLibrary, const void*, std::uint32_t, const FontOpenDetail*, FontHandle*);
int APS5_VABI sceFontCloseFont(FontHandle);
int APS5_VABI sceFontBindRenderer(FontHandle, FontRenderer);
int APS5_VABI sceFontUnbindRenderer(FontHandle);
int APS5_VABI sceFontRenderCharGlyphImage(FontHandle, std::uint32_t, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
int APS5_VABI sceFontRenderCharGlyphImageVertical(FontHandle, std::uint32_t, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
int APS5_VABI sceFontDefineAttribute(FontHandle, int, int*);
int APS5_VABI sceFontGetAttribute(FontHandle, int, int*);
int APS5_VABI sceFontGetVerticalLayout(FontHandle, FontVerticalLayout*);
int APS5_VABI sceFontSetScalePixel(FontHandle, float, float);
int APS5_VABI sceFontGenerateCharGlyph(FontHandle, std::uint32_t, const FontGenerateGlyphDetail*, FontGlyph*);
int APS5_VABI sceFontGlyphDefineAttribute(FontGlyph, int, int*);
int APS5_VABI sceFontGlyphGetAttribute(FontGlyph, int, int*);
int APS5_VABI sceFontDeleteGlyph(const FontMemory*, FontGlyph*);
void APS5_VABI sceFontRenderSurfaceInit(FontRenderSurface*, void*, int, int, int, int);
void APS5_VABI sceFontRenderSurfaceSetScissor(FontRenderSurface*, int, int, int, int);
int APS5_VABI sceFontStyleFrameInit(FontStyleFrame*);
int APS5_VABI sceFontStyleFrameSetScalePixel(FontStyleFrame*, float, float);
int APS5_VABI sceFontStyleFrameSetEffectSlant(FontStyleFrame*, float);
int APS5_VABI sceFontGlyphRenderImage(FontGlyph, FontStyleFrame*, FontRenderer, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
int APS5_VABI sceFontGlyphRenderImageHorizontal(FontGlyph, FontStyleFrame*, FontRenderer, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
int APS5_VABI sceFontGlyphRenderImageVertical(FontGlyph, FontStyleFrame*, FontRenderer, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
const void* APS5_VABI sceFontSelectLibraryFt(int);
const void* APS5_VABI sceFontSelectRendererFt(int);
}

static void Check(bool value, int line) {
    if (value) return;
    std::fprintf(stderr, "Font glyph render check failed at line %d\n", line);
    std::abort();
}
#define Require(value) Check((value), __LINE__)

static int allocations = 0;
static void* APS5_VABI Allocate_nid_no_patch(void*, std::uint32_t size) {
    ++allocations;
    return std::malloc(size);
}
static void APS5_VABI Release_nid_no_patch(void*, void* pointer) {
    if (pointer) --allocations;
    std::free(pointer);
}

static void Put16(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value));
}

static void Put32(std::vector<unsigned char>& out, std::uint32_t value) {
    Put16(out, value >> 16);
    Put16(out, value & 0xFFFFu);
}

static void PutAll16(std::vector<unsigned char>& out, std::initializer_list<int> values) {
    for (const int value : values) Put16(out, static_cast<std::uint32_t>(static_cast<std::uint16_t>(value)));
}

static std::vector<unsigned char> SquareGlyphFont(bool vertical) {
    std::vector<unsigned char> head;
    Put32(head, 0x00010000u);
    Put32(head, 0x00010000u);
    Put32(head, 0);
    Put32(head, 0x5F0F3CF5u);
    PutAll16(head, {0x000B, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 500, 800, 0, 8, 2, 0, 0});
    std::vector<unsigned char> hhea;
    Put32(hhea, 0x00010000u);
    PutAll16(hhea, {800, -200, 0, 500, 0, 0, 500, 1, 0, 0, 0, 0, 0, 0, 0, 2});
    std::vector<unsigned char> maxp;
    Put32(maxp, 0x00010000u);
    PutAll16(maxp, {2, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
    std::vector<unsigned char> hmtx;
    PutAll16(hmtx, {500, 0, 500, 100});
    std::vector<unsigned char> glyf;
    PutAll16(glyf, {1, 100, 0, 400, 700, 3, 0});
    glyf.insert(glyf.end(), {1, 1, 1, 1});
    PutAll16(glyf, {100, 0, 300, 0, 0, 700, 0, -700});
    std::vector<unsigned char> loca;
    PutAll16(loca, {0, 0, static_cast<int>(glyf.size() / 2)});
    std::vector<unsigned char> cmap;
    PutAll16(cmap, {0, 1, 3, 1});
    Put32(cmap, 12);
    PutAll16(cmap, {4, 32, 0, 4, 4, 1, 0, 'A', 0xFFFF, 0, 'A', 0xFFFF, 1 - 'A', 1, 0, 0});
    auto vhea = hhea;
    vhea[10] = 3;
    vhea[11] = 232;
    std::vector<unsigned char> vmtx;
    PutAll16(vmtx, {1000, 150, 1000, 150});
    std::vector<std::pair<const char*, const std::vector<unsigned char>*>> tables = {
        {"cmap", &cmap}, {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
    if (vertical) {
        tables.emplace_back("vhea", &vhea);
        tables.emplace_back("vmtx", &vmtx);
    }
    std::vector<unsigned char> font;
    Put32(font, 0x00010000u);
    const int searchRange = vertical ? 128 : 64;
    PutAll16(font, {static_cast<int>(tables.size()), searchRange, vertical ? 3 : 2, static_cast<int>(tables.size()) * 16 - searchRange});
    std::uint32_t offset = 12 + static_cast<std::uint32_t>(tables.size()) * 16;
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), tag, tag + 4);
        Put32(font, 0);
        Put32(font, offset);
        Put32(font, static_cast<std::uint32_t>(data->size()));
        offset += (static_cast<std::uint32_t>(data->size()) + 3u) & ~3u;
    }
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), data->begin(), data->end());
        font.resize((font.size() + 3u) & ~std::size_t{3});
    }
    return font;
}

struct Surface {
    static constexpr int Width = 128;
    static constexpr int Height = 128;
    static constexpr int Pitch = Width * 4 + 8;
    std::array<unsigned char, 16> before;
    std::array<unsigned char, Pitch * Height> pixels;
    std::array<unsigned char, 16> after;
    FontRenderSurface surface{};

    Surface() {
        before.fill(0xa5);
        after.fill(0xa5);
        Clear();
        sceFontRenderSurfaceInit(&surface, pixels.data(), Pitch, 4, Width, Height);
    }

    void Clear() { pixels.fill(0x33); }

    void CheckRectangle(int x, int y, int w, int h) const {
        for (int row = 0; row < Height; ++row) {
            for (int column = 0; column < Width; ++column) {
                const bool inside = column >= x && column < x + w && row >= y && row < y + h;
                for (int byte = 0; byte < 4; ++byte) Require(pixels[row * Pitch + column * 4 + byte] == (inside ? 0xff : 0x33));
            }
            for (int column = Width * 4; column < Pitch; ++column) Require(pixels[row * Pitch + column] == 0x33);
        }
        for (const auto value : before) Require(value == 0xa5);
        for (const auto value : after) Require(value == 0xa5);
    }
};

static void Run() {
    const FontMemoryInterface interface{Allocate_nid_no_patch, Release_nid_no_patch, nullptr, nullptr, nullptr, nullptr};
    FontMemory memory{};
    Require(sceFontMemoryInit(&memory, nullptr, 0, &interface, nullptr, nullptr, nullptr) == SCE_FONT_OK);
    FontLibrary library = nullptr;
    Require(sceFontCreateLibrary(&memory, sceFontSelectLibraryFt(0), &library) == SCE_FONT_OK);
    Require(sceFontSupportExternalFonts(library, 2, 0x52) == SCE_FONT_OK);
    FontRenderer renderer = nullptr;
    Require(sceFontCreateRenderer(&memory, sceFontSelectRendererFt(0), &renderer) == SCE_FONT_OK);
    for (const bool vertical : {false, true}) {
        const auto fontBytes = SquareGlyphFont(vertical);
        FontHandle font = nullptr;
        Require(sceFontOpenFontMemory(library, fontBytes.data(), static_cast<std::uint32_t>(fontBytes.size()), nullptr, &font) == SCE_FONT_OK);
        Require(sceFontSetScalePixel(font, 100.0f, 100.0f) == SCE_FONT_OK);
        Surface cachedSurface;
        FontGlyphMetrics cachedMetrics{};
        FontRenderOutput cachedOutput{};
        Require(sceFontBindRenderer(font, renderer) == SCE_FONT_OK);
        Require(sceFontRenderCharGlyphImage(font, 'A', &cachedSurface.surface, 20, 10, &cachedMetrics, &cachedOutput) == SCE_FONT_OK);
        const auto cachedPixels = cachedSurface.pixels;
        const auto* nativeFont = reinterpret_cast<const Font::FontHandleNative*>(font);
        Require((nativeFont->cached_style.cache_flags_and_direction & 0xff) != 0);
        int attribute = -1;
        Require(sceFontGetAttribute(font, 0x40, &attribute) == SCE_FONT_OK && attribute == 0x40);
        Require(sceFontDefineAttribute(font, 0x41, &attribute) == SCE_FONT_OK && attribute == 0x40);
        Require((nativeFont->cached_style.cache_flags_and_direction & 0xff) == 0);
        Require(sceFontGetAttribute(font, 0x40, &attribute) == SCE_FONT_OK && attribute == 0x41);
        Require(sceFontGetAttribute(font, 0x41, &attribute) == SCE_FONT_OK && attribute == 0x41);
        Require(sceFontDefineAttribute(font, 0x11, &attribute) == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION && attribute == 0);
        Require(sceFontGetAttribute(font, 0x41, &attribute) == SCE_FONT_OK && attribute == 0x41);
        Require(sceFontGetAttribute(font, 0x11, &attribute) == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION && attribute == 0);
        Require(sceFontGetAttribute(font, 0x40, nullptr) == SCE_FONT_ERROR_INVALID_PARAMETER);
        Require(sceFontGetAttribute(nullptr, 0x40, &attribute) == SCE_FONT_ERROR_INVALID_FONT_HANDLE && attribute == 0);
        Require(sceFontDefineAttribute(nullptr, 0x40, &attribute) == SCE_FONT_ERROR_INVALID_FONT_HANDLE && attribute == 0);
        FontGlyph verticalGlyph = nullptr;
        FontVerticalLayout verticalLayout{};
        if (vertical) {
            Require(sceFontGetVerticalLayout(font, &verticalLayout) == SCE_FONT_OK);
            cachedSurface.Clear();
            Require(sceFontRenderCharGlyphImage(font, 'A', &cachedSurface.surface, 80 - verticalLayout.baselineOffsetX, 90, &cachedMetrics, &cachedOutput) == SCE_FONT_OK);
            const auto verticalCharPixels = cachedSurface.pixels;
            Require((nativeFont->cached_style.cache_flags_and_direction & 0xff) != 0);
            cachedSurface.Clear();
            Require(sceFontRenderCharGlyphImageVertical(font, 'A', &cachedSurface.surface, 80, 90, &cachedMetrics, &cachedOutput) == SCE_FONT_OK);
            Require(cachedSurface.pixels == verticalCharPixels);
            Require(sceFontGenerateCharGlyph(font, 'A', nullptr, &verticalGlyph) == SCE_FONT_OK);
        }
        Require(sceFontDefineAttribute(font, 0x40, &attribute) == SCE_FONT_OK && attribute == 0x41);
        Require((nativeFont->cached_style.cache_flags_and_direction & 0xff) == 0);
        cachedSurface.Clear();
        Require(sceFontRenderCharGlyphImage(font, 'A', &cachedSurface.surface, 20, 10, &cachedMetrics, &cachedOutput) == SCE_FONT_OK);
        Require(cachedSurface.pixels == cachedPixels);
        Require(sceFontUnbindRenderer(font) == SCE_FONT_OK);
        Require(sceFontDefineAttribute(font, 0x40, &attribute) == SCE_FONT_OK && attribute == 0x40);
        Require(sceFontDefineAttribute(font, 0x40, nullptr) == SCE_FONT_OK);
        Require(sceFontGetAttribute(font, 0x41, &attribute) == SCE_FONT_OK && attribute == 0x40);
        FontGlyph glyph = nullptr;
        Require(sceFontGenerateCharGlyph(font, 'A', nullptr, &glyph) == SCE_FONT_OK);
        struct AttributeOutput { int value; std::uint32_t canary; } glyphAttribute{-1, 0xfeedcafe};
        Require(sceFontGlyphGetAttribute(glyph, 0x41, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x40);
        Require(glyphAttribute.canary == 0xfeedcafe);
        Require(sceFontGlyphGetAttribute(nullptr, 0x40, &glyphAttribute.value) == SCE_FONT_ERROR_INVALID_GLYPH && glyphAttribute.value == 0);
        Require(sceFontGlyphGetAttribute(glyph, 0x40, nullptr) == SCE_FONT_ERROR_INVALID_PARAMETER);
        Require(sceFontGlyphDefineAttribute(glyph, 0x11, &glyphAttribute.value) == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION && glyphAttribute.value == 0);
        Require(sceFontGlyphGetAttribute(glyph, 0x11, &glyphAttribute.value) == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION && glyphAttribute.value == 0);
        Require(sceFontGlyphGetAttribute(glyph, 0x40, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x40);
        Require(sceFontGlyphDefineAttribute(glyph, 0x40, nullptr) == SCE_FONT_OK);
        Surface surface;
        FontGlyphMetrics metrics{};
        FontRenderOutput output{};
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, 20, 90, &metrics, &output) == SCE_FONT_OK);
        surface.CheckRectangle(30, 20, 30, 70);
        Require(metrics.width == 30 && metrics.height == 70 && metrics.Horizontal.advance == 50);
        Require(output.UpdateRect.x == 30 && output.UpdateRect.y == 20 && output.UpdateRect.w == 30 && output.UpdateRect.h == 70);
        Require(output.SurfaceImage.address == surface.pixels.data() + 20 * Surface::Pitch + 30 * 4);
        const auto firstPixels = surface.pixels;
        std::array<unsigned char, 136 * 128> grayPixels;
        grayPixels.fill(0x33);
        FontRenderSurface gray{};
        sceFontRenderSurfaceInit(&gray, grayPixels.data(), 136, 1, 128, 128);
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &gray, 20, 90, &metrics, &output) == SCE_FONT_OK);
        for (int row = 0; row < 128; ++row) {
            for (int column = 0; column < 136; ++column) {
                const bool inside = column >= 30 && column < 60 && row >= 20 && row < 90;
                Require(grayPixels[row * 136 + column] == (inside ? 0xff : 0x33));
            }
        }
        surface.Clear();
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, 20.5f, 90.5f, &metrics, &output) == SCE_FONT_OK);
        Require(output.UpdateRect.x == 30 && output.UpdateRect.y == 20 && output.UpdateRect.w == 31 && output.UpdateRect.h == 71);
        Require(surface.pixels[20 * Surface::Pitch + 30 * 4] > 0 && surface.pixels[20 * Surface::Pitch + 30 * 4] < 255);
        Require(surface.pixels[21 * Surface::Pitch + 31 * 4] == 255);
        Require(sceFontSetScalePixel(font, 200.0f, 200.0f) == SCE_FONT_OK);
        Require(sceFontCloseFont(font) == SCE_FONT_OK);
        surface.Clear();
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, 20, 90, &metrics, &output) == SCE_FONT_OK);
        Require(surface.pixels == firstPixels);
        FontStyleFrame frame{};
        Require(sceFontStyleFrameInit(&frame) == SCE_FONT_OK);
        Require(sceFontStyleFrameSetScalePixel(&frame, 50, 50) == SCE_FONT_OK);
        surface.Clear();
        Require(sceFontGlyphRenderImageHorizontal(glyph, &frame, renderer, &surface.surface, 20, 90, &metrics, &output) == SCE_FONT_OK);
        surface.CheckRectangle(25, 55, 15, 35);
        Require(metrics.Horizontal.advance == 25);
        Require(sceFontStyleFrameSetScalePixel(&frame, 1000000, 1000000) == SCE_FONT_OK);
        Require(sceFontGlyphRenderImageHorizontal(glyph, &frame, renderer, &surface.surface, 20, 90, &metrics, &output) == SCE_FONT_ERROR_INVALID_PARAMETER);
        Require(sceFontStyleFrameSetScalePixel(&frame, 50, 50) == SCE_FONT_OK);
        surface.Clear();
        Require(sceFontGlyphRenderImage(glyph, nullptr, renderer, &surface.surface, 20, 10, &metrics, &output) == SCE_FONT_OK);
        Require(surface.pixels == firstPixels);
        surface.Clear();
        sceFontRenderSurfaceSetScissor(&surface.surface, 40, 40, 10, 12);
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, 20, 90, &metrics, &output) == SCE_FONT_OK);
        surface.CheckRectangle(40, 40, 10, 12);
        Require(output.UpdateRect.x == 40 && output.UpdateRect.y == 40 && output.UpdateRect.w == 10 && output.UpdateRect.h == 12);
        surface.Clear();
        sceFontRenderSurfaceSetScissor(&surface.surface, 0, 0, Surface::Width, Surface::Height);
        const int verticalResult = sceFontGlyphRenderImageVertical(glyph, nullptr, renderer, &surface.surface, 80, 20, &metrics, &output);
        if (vertical) {
            Require(verticalResult == SCE_FONT_OK);
            surface.CheckRectangle(65, 35, 30, 70);
            Require(metrics.Vertical.advance == 100);
            const auto verticalPixels = surface.pixels;
            surface.Clear();
            Require(sceFontGlyphRenderImage(verticalGlyph, nullptr, renderer, &surface.surface, 80 - verticalLayout.baselineOffsetX, 20, &metrics, &output) == SCE_FONT_OK);
            Require(surface.pixels == verticalPixels);
            Require(sceFontGlyphGetAttribute(verticalGlyph, 0x40, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x41);
            Require(sceFontGlyphDefineAttribute(verticalGlyph, 0x40, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x41);
            Require(glyphAttribute.canary == 0xfeedcafe);
            surface.Clear();
            Require(sceFontGlyphRenderImage(verticalGlyph, nullptr, renderer, &surface.surface, 20, 10, &metrics, &output) == SCE_FONT_OK);
            Require(surface.pixels == firstPixels);
            Require(sceFontGlyphDefineAttribute(verticalGlyph, 0x41, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x40);
            surface.Clear();
            Require(sceFontGlyphRenderImage(verticalGlyph, nullptr, renderer, &surface.surface, 80 - verticalLayout.baselineOffsetX, 20, &metrics, &output) == SCE_FONT_OK);
            Require(surface.pixels == verticalPixels);
            Require(sceFontGlyphGetAttribute(glyph, 0x40, &glyphAttribute.value) == SCE_FONT_OK && glyphAttribute.value == 0x40);
            Require(sceFontDeleteGlyph(nullptr, &verticalGlyph) == SCE_FONT_OK && verticalGlyph == nullptr);
        } else {
            Require(verticalResult == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION);
            surface.CheckRectangle(0, 0, 0, 0);
        }
        surface.Clear();
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, -1000, -1000, &metrics, &output) == SCE_FONT_OK);
        Require(output.UpdateRect.w == 0 && output.UpdateRect.h == 0 && output.SurfaceImage.address == surface.pixels.data());
        surface.CheckRectangle(0, 0, 0, 0);
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, nullptr, &surface.surface, 0, 0, &metrics, &output) == SCE_FONT_ERROR_INVALID_RENDERER);
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &surface.surface, std::numeric_limits<float>::quiet_NaN(), 0, &metrics, &output) == SCE_FONT_ERROR_INVALID_PARAMETER);
        auto invalidSurface = surface.surface;
        invalidSurface.widthByte = 1;
        Require(sceFontGlyphRenderImageHorizontal(glyph, nullptr, renderer, &invalidSurface, 0, 0, &metrics, &output) == SCE_FONT_ERROR_NO_SUPPORT_SURFACE);
        Require(sceFontStyleFrameSetEffectSlant(&frame, 0.5f) == SCE_FONT_OK);
        Require(sceFontGlyphRenderImageHorizontal(glyph, &frame, renderer, &surface.surface, 0, 0, &metrics, &output) == SCE_FONT_ERROR_NO_SUPPORT_FUNCTION);
        const auto deletedGlyph = glyph;
        Require(sceFontDeleteGlyph(nullptr, &glyph) == SCE_FONT_OK && glyph == nullptr);
        Require(sceFontGlyphRenderImageHorizontal(deletedGlyph, nullptr, renderer, &surface.surface, 0, 0, &metrics, &output) == SCE_FONT_ERROR_INVALID_GLYPH);
        Require(sceFontGlyphDefineAttribute(deletedGlyph, 0x41, &glyphAttribute.value) == SCE_FONT_ERROR_INVALID_GLYPH && glyphAttribute.value == 0);
        Require(sceFontGlyphGetAttribute(deletedGlyph, 0x41, &glyphAttribute.value) == SCE_FONT_ERROR_INVALID_GLYPH && glyphAttribute.value == 0);
    }
    Require(sceFontDestroyRenderer(&renderer) == SCE_FONT_OK);
    Require(sceFontDestroyLibrary(&library) == SCE_FONT_OK);
    Require(sceFontMemoryTerm(&memory) == SCE_FONT_OK);
    Require(allocations == 0);
}

int main() {
    try {
        Run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
