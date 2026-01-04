#include "PngToBmpConverter.h"

#include <HardwareSerial.h>
#include <PNGdec.h>
#include <SdFat.h>

#include <cstdio>
#include <cstring>

#include "BitmapHelpers.h"
#include "BmpWriter.h"

static void write2BitRow(Print& out, const uint8_t* pixels, int width) {
  // Pack 4 pixels per byte (2 bits each)
  int x = 0;
  while (x < width) {
    uint8_t packed = 0;
    // Pack up to 4 pixels into one byte (MSB first)
    for (int shift = 6; shift >= 0 && x < width; shift -= 2, x++) {
      packed |= (pixels[x] & 0x03) << shift;
    }
    out.write(packed);
  }

  // Add row padding (BMP rows must be aligned to 4 bytes)
  const int bytesPerRow = (width * 2 + 7) / 8;      // Round up to nearest byte
  const int paddedRow = (bytesPerRow + 3) / 4 * 4;  // Round up to multiple of 4
  const int padding = paddedRow - bytesPerRow;
  for (int i = 0; i < padding; i++) {
    out.write(static_cast<uint8_t>(0));
  }
}

// ============================================================================
// COLOR CONVERSION HELPERS
// ============================================================================

// Convert RGB to grayscale using weighted formula
static inline uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) {
  // Weighted average: R*0.25 + G*0.50 + B*0.25
  // Using integer math: (R*25 + G*50 + B*25) / 100
  return (r * 25 + g * 50 + b * 25) / 100;
}

// Blend foreground color with white background using alpha
static inline uint8_t blendAlpha(uint8_t fg, uint8_t alpha) {
  // result = (fg * alpha + 255 * (255 - alpha)) / 255
  // Simplifies to: (fg * alpha) / 255 + (255 - alpha)
  return ((fg * alpha) >> 8) + (255 - alpha);
}

// Context for draw callback
struct PngDrawContext {
  Print* bmpOut;               // BMP output stream
  AtkinsonDitherer* ditherer;  // Dithering engine
  uint8_t* rowBuffer;          // 2-bit output buffer for one row
  int width;                   // Output width
  int height;                  // Output height
};

// Note: We use openRAM() instead of custom file callbacks for simplicity
// PNG files in EPUBs are typically small (< 100KB), so loading into RAM is acceptable

// ============================================================================
// IMAGE PROCESSING OPTIONS
// ============================================================================
constexpr bool USE_ATKINSON = true;     // Use Atkinson dithering
constexpr bool USE_PRESCALE = false;    // TEMPORARILY DISABLED - Pre-scale to fit display
constexpr int TARGET_MAX_WIDTH = 480;   // Max width for display
constexpr int TARGET_MAX_HEIGHT = 800;  // Max height for display
constexpr int MAX_IMAGE_WIDTH = 2048;   // Safety limit
constexpr int MAX_IMAGE_HEIGHT = 3072;  // Safety limit
// ============================================================================

// PNG draw callback - process each scanline
int pngDraw(PNGDRAW* pDraw) {
  if (!pDraw || !pDraw->pUser) {
    return 0;
  }

  auto* ctx = static_cast<PngDrawContext*>(pDraw->pUser);
  const int y = pDraw->y;
  const int width = pDraw->iWidth;
  const uint8_t* pixels = pDraw->pPixels;

  // Convert pixels to grayscale and apply dithering
  for (int x = 0; x < width; x++) {
    uint8_t gray;

    // Convert pixel to grayscale based on format
    if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR) {
      // RGB format (3 bytes per pixel)
      const uint8_t r = pixels[x * 3];
      const uint8_t g = pixels[x * 3 + 1];
      const uint8_t b = pixels[x * 3 + 2];
      gray = rgbToGray(r, g, b);
    } else if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
      // RGBA format (4 bytes per pixel)
      const uint8_t r = pixels[x * 4];
      const uint8_t g = pixels[x * 4 + 1];
      const uint8_t b = pixels[x * 4 + 2];
      const uint8_t a = pixels[x * 4 + 3];
      // Blend with white background
      const uint8_t r_blend = blendAlpha(r, a);
      const uint8_t g_blend = blendAlpha(g, a);
      const uint8_t b_blend = blendAlpha(b, a);
      gray = rgbToGray(r_blend, g_blend, b_blend);
    } else if (pDraw->iPixelType == PNG_PIXEL_GRAYSCALE) {
      // Already grayscale
      gray = pixels[x];
    } else {
      // Unsupported format, use white
      gray = 255;
    }

    // Apply brightness/contrast/gamma adjustments
    gray = adjustPixel(gray);

    // Apply dithering and quantize to 2-bit
    ctx->rowBuffer[x] = ctx->ditherer->processPixel(gray, x);
  }

  // Write row to BMP output
  write2BitRow(*ctx->bmpOut, ctx->rowBuffer, width);

  // Advance ditherer to next row
  ctx->ditherer->nextRow();

  return 1;  // Continue decoding
}

// ============================================================================
// IMAGE CONVERSION
// ============================================================================

// Core function: Convert PNG file to 2-bit BMP
// NOTE: This function expects pngFile to be a temp file on the SD card
// The caller should extract the PNG from EPUB to a temp file first
bool PngToBmpConverter::pngFileToBmpStream(FsFile& pngFile, Print& bmpOut) {
  Serial.printf("[%lu] [PNG] Converting PNG to BMP\n", millis());

  // Color processing settings are configured in BitmapHelpers.cpp:
  // USE_BRIGHTNESS=true, BRIGHTNESS_BOOST=10, GAMMA_CORRECTION=true, CONTRAST_FACTOR=1.15f

  // Check file is valid and get size
  if (!pngFile.isOpen()) {
    Serial.printf("[%lu] [PNG] PNG file is not open\n", millis());
    return false;
  }

  const int32_t fileSize = pngFile.size();
  if (fileSize <= 0) {
    Serial.printf("[%lu] [PNG] Invalid PNG file size: %d\n", millis(), fileSize);
    return false;
  }

  Serial.printf("[%lu] [PNG] Loading PNG file into RAM (%d bytes)\n", millis(), fileSize);

  // Read entire PNG file into memory
  // Most PNG images in EPUBs are < 100KB, which is acceptable for our RAM budget
  auto* pngData = static_cast<uint8_t*>(malloc(fileSize));
  if (!pngData) {
    Serial.printf("[%lu] [PNG] Failed to allocate %d bytes for PNG data\n", millis(), fileSize);
    return false;
  }

  pngFile.rewind();
  const int32_t bytesRead = pngFile.read(pngData, fileSize);
  pngFile.close();  // Close file early to free up resources

  if (bytesRead != fileSize) {
    Serial.printf("[%lu] [PNG] Failed to read PNG file: read %d bytes, expected %d\n", millis(), bytesRead, fileSize);
    free(pngData);
    return false;
  }

  // Open PNG from RAM
  // Allocate PNG decoder on heap - it's ~36KB and would overflow the stack
  PNG* png = new PNG();
  if (!png) {
    Serial.printf("[%lu] [PNG] Failed to allocate PNG decoder\n", millis());
    free(pngData);
    return false;
  }

  // Open with callback for line-by-line processing
  const int rc = png->openRAM(pngData, fileSize, pngDraw);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[%lu] [PNG] Failed to open PNG from RAM: error code %d\n", millis(), png->getLastError());
    delete png;
    free(pngData);
    return false;
  }

  const int srcWidth = png->getWidth();
  const int srcHeight = png->getHeight();
  const int bpp = png->getBpp();
  const bool hasAlpha = png->hasAlpha();

  Serial.printf("[%lu] [PNG] PNG dimensions: %dx%d, bpp: %d, alpha: %d\n", millis(), srcWidth, srcHeight, bpp,
                hasAlpha);

  // Safety limits
  if (srcWidth > MAX_IMAGE_WIDTH || srcHeight > MAX_IMAGE_HEIGHT) {
    Serial.printf("[%lu] [PNG] Image too large (%dx%d), max supported: %dx%d\n", millis(), srcWidth, srcHeight,
                  MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT);
    png->close();
    delete png;
    free(pngData);
    return false;
  }

  // Note: For now, we don't pre-scale PNGs - just output at native resolution
  // The display system will handle scaling/centering
  const int outWidth = srcWidth;
  const int outHeight = srcHeight;

  Serial.printf("[%lu] [PNG] Output dimensions: %dx%d\n", millis(), outWidth, outHeight);

  // Write BMP header
  writeBmpHeader2bit(bmpOut, outWidth, outHeight);

  // Allocate ditherer
  AtkinsonDitherer* ditherer = new AtkinsonDitherer(outWidth);
  if (!ditherer) {
    Serial.printf("[%lu] [PNG] Failed to allocate ditherer\n", millis());
    png->close();
    delete png;
    free(pngData);
    return false;
  }

  // Allocate row buffer for 2-bit output
  auto* rowBuffer = static_cast<uint8_t*>(malloc(outWidth));
  if (!rowBuffer) {
    Serial.printf("[%lu] [PNG] Failed to allocate row buffer (%d bytes)\n", millis(), outWidth);
    delete ditherer;
    png->close();
    delete png;
    free(pngData);
    return false;
  }

  // Setup context for callback
  PngDrawContext ctx;
  ctx.bmpOut = &bmpOut;
  ctx.ditherer = ditherer;
  ctx.rowBuffer = rowBuffer;
  ctx.width = outWidth;
  ctx.height = outHeight;

  // Decode with callback - this will call pngDraw for each scanline
  Serial.printf("[%lu] [PNG] Starting line-by-line decode and conversion\n", millis());
  const int decodeRc = png->decode(&ctx, 0);
  const bool success = (decodeRc == PNG_SUCCESS);

  // Cleanup buffers
  free(rowBuffer);
  delete ditherer;

  if (!success) {
    Serial.printf("[%lu] [PNG] PNG decode failed: error code %d\n", millis(), png->getLastError());
  } else {
    Serial.printf("[%lu] [PNG] Decode succeeded!\n", millis());
  }

  // Cleanup
  png->close();
  delete png;
  free(pngData);

  return success;
}
