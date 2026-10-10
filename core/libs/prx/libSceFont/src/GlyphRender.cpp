#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "prx/libSceFont/include/FontFreeType.hpp"
#include "prx/libSceFont/include/FontInternal.hpp"

int Font::CaptureGeneratedGlyph(GeneratedGlyph& glyph, FontHandle handle, const FontState& state) {
    if (!state.face || !state.faceData) return SCE_FONT_ERROR_NO_SUPPORT_GLYPH;
    FontState snapshot;
    LoadStateFace(snapshot, state.faceData, static_cast<std::uint32_t>(state.face->face_index));
    if (!snapshot.face) return SCE_FONT_ERROR_ALLOCATION_FAILED;
    const auto* font = GetNativeFont(handle);
    float width = 0.0f;
    float height = 0.0f;
    const int sizeResult = StyleStateGetScalePixel(&font->style, &width, &height);
    if (sizeResult != SCE_FONT_OK) return sizeResult;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f || width > 65535.0f || height > 65535.0f) return SCE_FONT_ERROR_INVALID_PARAMETER;
    const FT_F26Dot6 charWidth = static_cast<FT_F26Dot6>(width * 64.0f);
    const FT_F26Dot6 charHeight = static_cast<FT_F26Dot6>(height * 64.0f);
    if (SetCharSizeCompat(snapshot.face, charWidth, charHeight, 72, 72) != 0) return SCE_FONT_ERROR_NO_SUPPORT_GLYPH;
    const auto index = ResolveGlyphIndexWithFallback(snapshot.face, glyph.codepoint);
    if (index == 0 || FT_Load_Glyph(snapshot.face, index, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) return SCE_FONT_ERROR_NO_SUPPORT_GLYPH;
    FT_Glyph image = nullptr;
    if (FT_Get_Glyph(snapshot.face->glyph, &image) != 0) return SCE_FONT_ERROR_ALLOCATION_FAILED;
    glyph.image.reset(image);
    glyph.imageMetrics = snapshot.face->glyph->metrics;
    glyph.hasVerticalMetrics = FT_HAS_VERTICAL(snapshot.face);
    glyph.defaultVertical = (font->flags & 0x8000u) != 0;
    glyph.hasUnsupportedEffects = font->style.slant_ratio != 0.0f || font->style.effect_weight_x != 0.0f || font->style.effect_weight_y != 0.0f;
    glyph.glyph.scale_x = width;
    glyph.glyph.base_scale = height;
    std::uint8_t layout[HORIZONTAL_LAYOUT_SIZE]{};
    const int horizontalResult = ComputeHorizontalLayout(handle, &font->style, layout);
    if (horizontalResult != SCE_FONT_OK) return horizontalResult;
    glyph.horizontalBaseline = LoadFloat(layout, HORIZONTAL_BASELINE);
    if (glyph.hasVerticalMetrics) {
        const int verticalResult = ComputeVerticalLayout(handle, &font->style, layout);
        if (verticalResult != SCE_FONT_OK) return verticalResult;
        glyph.verticalBaseline = LoadFloat(layout, VERTICAL_BASELINE_OFFSET_X);
    }
    return SCE_FONT_OK;
}

int Font::RenderGeneratedGlyph(FontGlyph handle, const FontStyleFrame* frame, FontRenderer rendererHandle, FontRenderSurface* surface,
                float x, float y, FontGlyphMetrics* metrics, FontRenderOutput* output, int direction) {
    ClearRenderOutputs(metrics, output);
    const auto* glyph = TryGetGeneratedGlyph(handle);
    if (!glyph || !glyph->image) return SCE_FONT_ERROR_INVALID_GLYPH;
    const auto* renderer = static_cast<const RendererNative*>(rendererHandle);
    if (!renderer || renderer->magic != RENDERER_MAGIC) return SCE_FONT_ERROR_INVALID_RENDERER;
    if (!surface || !metrics || !output || (frame && !ValidStyleFrame(frame))) return SCE_FONT_ERROR_INVALID_PARAMETER;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000000.0f || std::abs(y) > 1000000000.0f) return SCE_FONT_ERROR_INVALID_PARAMETER;
    if (!surface->buffer || surface->width <= 0 || surface->height <= 0 || surface->widthByte <= 0 ||
        (surface->pixelSizeByte != 1 && surface->pixelSizeByte != 4) ||
        static_cast<std::int64_t>(surface->width) * surface->pixelSizeByte > surface->widthByte) return SCE_FONT_ERROR_NO_SUPPORT_SURFACE;
    const bool vertical = direction == 2 || (direction == 0 && glyph->defaultVertical);
    if (vertical && !glyph->hasVerticalMetrics) return SCE_FONT_ERROR_NO_SUPPORT_FUNCTION;
    if (glyph->hasUnsupportedEffects || (frame && (((frame->flags1 & STYLE_FRAME_FLAG_SLANT) != 0 && frame->slantRatio != 0.0f) ||
        ((frame->flags1 & STYLE_FRAME_FLAG_WEIGHT) != 0 && (frame->effectWeightX != 0.0f || frame->effectWeightY != 0.0f))))) return SCE_FONT_ERROR_NO_SUPPORT_FUNCTION;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (frame && (frame->flags1 & STYLE_FRAME_FLAG_SCALE) != 0) {
        StyleStateBlock style{};
        style.scale_unit = frame->scaleUnit;
        style.scale_w = frame->scalePixelW;
        style.scale_h = frame->scalePixelH;
        style.dpi_x = frame->hDpi;
        style.dpi_y = frame->vDpi;
        float width = 0.0f;
        float height = 0.0f;
        const int rc = StyleStateGetScalePixel(&style, &width, &height);
        if (rc != SCE_FONT_OK) return rc;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f || width > 65535.0f || height > 65535.0f) return SCE_FONT_ERROR_INVALID_PARAMETER;
        scaleX = width / glyph->glyph.scale_x;
        scaleY = height / glyph->glyph.base_scale;
    }
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.0f || scaleY <= 0.0f || scaleX > 32767.0f || scaleY > 32767.0f) return SCE_FONT_ERROR_INVALID_PARAMETER;
    if (direction == 0) {
        if (vertical) x += glyph->verticalBaseline * scaleX;
        else y += glyph->horizontalBaseline * scaleY;
    }
    FT_Glyph copy = nullptr;
    if (FT_Glyph_Copy(glyph->image.get(), &copy) != 0) return SCE_FONT_ERROR_ALLOCATION_FAILED;
    std::unique_ptr<FT_GlyphRec_, GlyphImageDeleter> image(copy);
    const FT_Matrix transform{static_cast<FT_Fixed>(std::llround(scaleX * 65536.0)), 0, 0, static_cast<FT_Fixed>(std::llround(scaleY * 65536.0))};
    FT_Vector offset{static_cast<FT_Pos>(std::llround((x - std::floor(x)) * 64.0)), -static_cast<FT_Pos>(std::llround((y - std::floor(y)) * 64.0))};
    if (vertical) {
        offset.x += static_cast<FT_Pos>(std::llround((glyph->imageMetrics.vertBearingX - glyph->imageMetrics.horiBearingX) * scaleX));
        offset.y -= static_cast<FT_Pos>(std::llround((glyph->imageMetrics.vertBearingY + glyph->imageMetrics.horiBearingY) * scaleY));
    }
    if (FT_Glyph_Transform(image.get(), &transform, &offset) != 0) return SCE_FONT_ERROR_NO_SUPPORT_GLYPH;
    FT_Glyph raster = image.release();
    const auto rasterResult = FT_Glyph_To_Bitmap(&raster, FT_RENDER_MODE_NORMAL, nullptr, true);
    image.reset(raster);
    if (rasterResult != 0) return SCE_FONT_ERROR_RENDERER_RENDER_FAILED;
    const auto* bitmapGlyph = reinterpret_cast<const FT_BitmapGlyphRec*>(image.get());
    const auto& bitmap = bitmapGlyph->bitmap;
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO) return SCE_FONT_ERROR_NO_SUPPORT_GLYPH;
    const auto& source = glyph->imageMetrics;
    const float unitX = scaleX / 64.0f;
    const float unitY = scaleY / 64.0f;
    metrics->width = source.width * unitX;
    metrics->height = source.height * unitY;
    metrics->Horizontal = {source.horiBearingX * unitX, source.horiBearingY * unitY, source.horiAdvance * unitX};
    metrics->Vertical = {source.vertBearingX * unitX, source.vertBearingY * unitY, source.vertAdvance * unitY};
    const auto left = static_cast<std::int64_t>(std::floor(x)) + bitmapGlyph->left;
    const auto top = static_cast<std::int64_t>(std::floor(y)) - bitmapGlyph->top;
    const auto clipLeft = std::min<std::uint32_t>(surface->sc_x0, surface->width);
    const auto clipTop = std::min<std::uint32_t>(surface->sc_y0, surface->height);
    const auto clipRight = std::min<std::uint32_t>(surface->sc_x1, surface->width);
    const auto clipBottom = std::min<std::uint32_t>(surface->sc_y1, surface->height);
    const auto startX = std::max<std::int64_t>(left, clipLeft);
    const auto startY = std::max<std::int64_t>(top, clipTop);
    const auto endX = std::min<std::int64_t>(left + bitmap.width, clipRight);
    const auto endY = std::min<std::int64_t>(top + bitmap.rows, clipBottom);
    const bool visible = endX > startX && endY > startY;
    if (visible) {
        const auto pitch = static_cast<std::ptrdiff_t>(bitmap.pitch);
        for (auto row = startY; row < endY; ++row) {
            const auto sourceRowIndex = pitch >= 0 ? row - top : bitmap.rows - 1 - (row - top);
            const auto* sourceRow = bitmap.buffer + sourceRowIndex * std::abs(pitch);
            auto* targetRow = static_cast<std::uint8_t*>(surface->buffer) + row * surface->widthByte;
            for (auto column = startX; column < endX; ++column) {
                const auto sourceColumn = column - left;
                const auto coverage = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                    ? (sourceRow[sourceColumn / 8] & (0x80u >> (sourceColumn % 8)) ? 255 : 0)
                    : sourceRow[sourceColumn];
                std::memset(targetRow + column * surface->pixelSizeByte, coverage, surface->pixelSizeByte);
            }
        }
    }
    output->SurfaceImage.address = static_cast<std::uint8_t*>(surface->buffer);
    output->SurfaceImage.widthByte = surface->widthByte;
    output->SurfaceImage.pixelSizeByte = surface->pixelSizeByte;
    if (visible) {
        output->UpdateRect = {static_cast<std::uint32_t>(startX), static_cast<std::uint32_t>(startY), static_cast<std::uint32_t>(endX - startX), static_cast<std::uint32_t>(endY - startY)};
        output->SurfaceImage.address += startY * surface->widthByte + startX * surface->pixelSizeByte;
    }
    output->ImageMetrics.bearingX = static_cast<float>(left) - x;
    output->ImageMetrics.bearingY = y - static_cast<float>(top);
    output->ImageMetrics.advance = vertical ? metrics->Vertical.advance : metrics->Horizontal.advance;
    output->ImageMetrics.stride = output->ImageMetrics.advance;
    output->ImageMetrics.width = bitmap.width;
    output->ImageMetrics.height = bitmap.rows;
    return SCE_FONT_OK;
}
