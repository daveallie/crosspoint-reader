#pragma once
#include <EpdFontFamily.h>
#include <HardwareSerial.h>
#include <Utf8.h>

inline int min(const int a, const int b) { return a < b ? a : b; }
inline int max(const int a, const int b) { return a > b ? a : b; }

enum EpdFontRendererMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

template <typename Renderable>
class EpdFontRenderer {
  Renderable& renderer;
  void renderChar(uint32_t cp, int* x, const int* y, bool pixelState, EpdFontStyle style = REGULAR,
                  EpdFontRendererMode mode = BW);

 public:
  const EpdFontFamily* fontFamily;
  explicit EpdFontRenderer(const EpdFontFamily* fontFamily, Renderable& renderer)
      : fontFamily(fontFamily), renderer(renderer) {}
  ~EpdFontRenderer() = default;
  void renderString(const char* string, int* x, int* y, bool pixelState = true, EpdFontStyle style = REGULAR,
                    EpdFontRendererMode mode = BW);
  void drawPixel(int x, int y, bool pixelState);
};

template <typename Renderable>
void EpdFontRenderer<Renderable>::renderString(const char* string, int* x, int* y, const bool pixelState,
                                               const EpdFontStyle style, const EpdFontRendererMode mode) {
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
    renderChar(cp, x, y, pixelState, style, mode);
  }

  *y += fontFamily->getData(style)->advanceY;
}

// TODO: Consolidate this with EpdRenderer implementation
template <typename Renderable>
void EpdFontRenderer<Renderable>::drawPixel(const int x, const int y, const bool pixelState) {
  uint8_t* frameBuffer = renderer.getFrameBuffer();

  // Early return if no framebuffer is set
  if (!frameBuffer) {
    Serial.printf("!!No framebuffer\n");
    return;
  }

  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    Serial.printf("!!Outside range (%d, %d)\n", x, y);
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  const int16_t rotatedX = y;
  const int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Calculate byte position and bit position
  const uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set or clear the bit
  if (pixelState) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= (1 << bitPosition);  // Set bit
  }
}

template <typename Renderable>
void EpdFontRenderer<Renderable>::renderChar(const uint32_t cp, int* x, const int* y, const bool pixelState,
                                             const EpdFontStyle style, const EpdFontRendererMode mode) {
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

  const int is2Bit = fontFamily->getData(style)->is2Bit;
  const uint32_t offset = glyph->dataOffset;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const uint8_t* bitmap = nullptr;
  bitmap = &fontFamily->getData(style)->bitmap[offset];

  if (bitmap != nullptr) {
    for (int glyphY = 0; glyphY < height; glyphY++) {
      int screenY = *y - glyph->top + glyphY;
      for (int glyphX = 0; glyphX < width; glyphX++) {
        const int pixelPosition = glyphY * width + glyphX;
        int screenX = *x + left + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bit_index = (3 - pixelPosition % 4) * 2;

          const uint8_t val = (byte >> bit_index) & 0x3;
          if (mode == BW && val > 0) {
            drawPixel(screenX, screenY, pixelState);
          } else if (mode == GRAYSCALE_MSB && val == 1) {
            // TODO: Not sure how this anti-aliasing goes on black backgrounds
            drawPixel(screenX, screenY, false);
          } else if (mode == GRAYSCALE_LSB && val == 2) {
            drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPosition / 8];
          const uint8_t bit_index = 7 - (pixelPosition % 8);

          if ((byte >> bit_index) & 1) {
            drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  *x += glyph->advanceX;
}
