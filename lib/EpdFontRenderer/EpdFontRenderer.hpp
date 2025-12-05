#pragma once
#include <EpdFontFamily.h>
#include <HardwareSerial.h>
#include <Utf8.h>
#include <miniz.h>

inline int min(const int a, const int b) { return a < b ? a : b; }
inline int max(const int a, const int b) { return a > b ? a : b; }

static tinfl_decompressor decomp;

template <typename Renderable>
class EpdFontRenderer {
  Renderable* renderer;
  void renderChar(uint32_t cp, int* x, const int* y, uint16_t color, EpdFontStyle style = REGULAR);

 public:
  const EpdFontFamily* fontFamily;
  explicit EpdFontRenderer(const EpdFontFamily* fontFamily, Renderable* renderer)
      : fontFamily(fontFamily), renderer(renderer) {}
  ~EpdFontRenderer() = default;
  void renderString(const char* string, int* x, int* y, uint16_t color, EpdFontStyle style = REGULAR);
};

inline int uncompress(uint8_t* dest, size_t uncompressedSize, const uint8_t* source, size_t sourceSize) {
  if (uncompressedSize == 0 || dest == nullptr || sourceSize == 0 || source == nullptr) {
    return -1;
  }
  tinfl_init(&decomp);

  // we know everything will fit into the buffer.
  const tinfl_status decomp_status =
      tinfl_decompress(&decomp, source, &sourceSize, dest, dest, &uncompressedSize,
                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (decomp_status != TINFL_STATUS_DONE) {
    return decomp_status;
  }
  return 0;
}

template <typename Renderable>
void EpdFontRenderer<Renderable>::renderString(const char* string, int* x, int* y, const uint16_t color,
                                               const EpdFontStyle style) {
  // cannot draw a NULL / empty string
  if (string == nullptr || *string == '\0') {
    return;
  }

  // no printable characters
  if (!fontFamily->hasPrintableChars(string, style)) {
    return;
  }

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&string)))) {
    renderChar(cp, x, y, color, style);
  }

  *y += fontFamily->getData(style)->advanceY;
}

template <typename Renderable>
void EpdFontRenderer<Renderable>::renderChar(const uint32_t cp, int* x, const int* y, uint16_t color,
                                             const EpdFontStyle style) {
  const EpdGlyph* glyph = fontFamily->getGlyph(cp, style);
  if (!glyph) {
    // TODO: Replace with fallback glyph property?
    glyph = fontFamily->getGlyph('?', style);
  }

  // no glyph?
  if (!glyph) {
    Serial.printf("No glyph for codepoint %d\n", cp);
    return;
  }

  const uint32_t offset = glyph->dataOffset;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const int byteWidth = width / 2 + width % 2;
  const unsigned long bitmapSize = byteWidth * height;
  const uint8_t* bitmap = nullptr;

  if (fontFamily->getData(style)->compressed) {
    auto* tmpBitmap = static_cast<uint8_t*>(malloc(bitmapSize));
    if (tmpBitmap == nullptr && bitmapSize) {
      Serial.println("Failed to allocate memory for decompression buffer");
      return;
    }

    uncompress(tmpBitmap, bitmapSize, &fontFamily->getData(style)->bitmap[offset], glyph->compressedSize);
    bitmap = tmpBitmap;
  } else {
    bitmap = &fontFamily->getData(style)->bitmap[offset];
  }

  if (bitmap != nullptr) {
    for (int localY = 0; localY < height; localY++) {
      int yy = *y - glyph->top + localY;
      const int startPos = *x + left;
      bool byteComplete = startPos % 2;
      int localX = max(0, -startPos);
      const int maxX = startPos + width;

      for (int xx = startPos; xx < maxX; xx++) {
        uint8_t bm = bitmap[localY * byteWidth + localX / 2];
        if ((localX & 1) == 0) {
          bm = bm & 0xF;
        } else {
          bm = bm >> 4;
        }

        if (bm) {
          renderer->drawPixel(xx, yy, color);
        }
        byteComplete = !byteComplete;
        localX++;
      }
    }

    if (fontFamily->getData(style)->compressed) {
      free(const_cast<uint8_t*>(bitmap));
    }
  }

  *x += glyph->advanceX;
}
