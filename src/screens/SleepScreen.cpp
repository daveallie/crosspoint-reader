#include "SleepScreen.h"

#include <GfxRenderer.h>
#include <SD.h>
#include <Arduino.h>

#include "CrossPointSettings.h"
#include "config.h"
#include "images/CrossLarge.h"

// BMP file header structure
#pragma pack(push, 1)
struct BMPHeader {
  uint16_t signature;      // 'BM'
  uint32_t fileSize;       // Size of the BMP file in bytes
  uint32_t reserved;       // Reserved
  uint32_t dataOffset;     // Offset to bitmap data
  uint32_t headerSize;     // Size of the header
  int32_t width;           // Width of the image
  int32_t height;          // Height of the image
  uint16_t planes;         // Number of color planes
  uint16_t bitsPerPixel;   // Bits per pixel
  uint32_t compression;    // Compression method
  uint32_t imageSize;      // Size of the image data
  int32_t xPixelsPerMeter; // Horizontal resolution
  int32_t yPixelsPerMeter; // Vertical resolution
  uint32_t totalColors;    // Number of colors in palette
  uint32_t importantColors;// Number of important colors
};
#pragma pack(pop)

// Load BMP file from SD card and rotate 90 degrees clockwise
// This rotation matches what we need for the e-ink display
// Returns a buffer with data formatted for the e-ink display (1bpp, MSB first)
uint8_t* loadBMP(const char* filename, int& width, int& height) {
  const unsigned long startTime = millis();
  Serial.printf("[%lu] [SleepScreen] Trying to load BMP: %s\n", millis(), filename);

  if (!SD.exists(filename)) {
    Serial.printf("[%lu] [SleepScreen] File not found: %s\n", millis(), filename);
    return nullptr;
  }

  File bmpFile = SD.open(filename);
  if (!bmpFile) {
    Serial.printf("[%lu] [SleepScreen] Failed to open file: %s\n", millis(), filename);
    return nullptr;
  }

  // Read BMP header
  BMPHeader header;
  bmpFile.read((uint8_t*)&header, sizeof(BMPHeader));

  // Check if this is a valid BMP file
  if (header.signature != 0x4D42) { // "BM" in little-endian
    Serial.printf("[%lu] [SleepScreen] Invalid BMP signature\n", millis());
    bmpFile.close();
    return nullptr;
  }

  // Check for supported bit depths
  if (header.bitsPerPixel != 1 && header.bitsPerPixel != 24) {
    Serial.printf("[%lu] [SleepScreen] Unsupported bit depth: %d\n", millis(), header.bitsPerPixel);
    bmpFile.close();
    return nullptr;
  }

  // Get image dimensions
  width = header.width;
  height = (header.height < 0) ? -header.height : header.height; // Handle top-down BMPs
  bool topDown = (header.height < 0);

  Serial.printf("[%lu] [SleepScreen] BMP dimensions: %dx%d, %d bits/pixel\n", millis(), width, height, header.bitsPerPixel);

  // Calculate destination dimensions based on rotation type
  int destWidth, destHeight;

  // 90 degree rotation to match display orientation
  destWidth = height;
  destHeight = width;

  // E-ink display: 1 bit per pixel (8 pixels per byte), MSB first format
  int bytesPerRow = (destWidth + 7) / 8;  // Round up to nearest byte
  int bufferSize = bytesPerRow * destHeight;

  // Allocate memory for the display image
  uint8_t* displayImage = (uint8_t*)malloc(bufferSize);
  if (!displayImage) {
    Serial.printf("[%lu] [SleepScreen] Failed to allocate memory for display image\n", millis());
    bmpFile.close();
    return nullptr;
  }

  // Initialize to all white (0xFF = all bits set to 1)
  memset(displayImage, 0xFF, bufferSize);

  // Calculate BMP file row size (padded to 4-byte boundaries)
  int bmpRowSize;
  if (header.bitsPerPixel == 1) {
    bmpRowSize = ((width + 31) / 32) * 4; // 1 bit per pixel, 4-byte alignment
  } else { // 24-bit
    bmpRowSize = ((width * 3 + 3) / 4) * 4; // 3 bytes per pixel (RGB), 4-byte alignment
  }

  // Optimized direct handling for 1bpp BMPs
  if (header.bitsPerPixel == 1) {
    // Calculate total file size needed for reading the whole bitmap at once
    const int totalBitmapSize = bmpRowSize * height;
    
    // Allocate a buffer for the entire bitmap
    uint8_t* bmpData = (uint8_t*)malloc(totalBitmapSize);
    if (!bmpData) {
      Serial.printf("[%lu] [SleepScreen] Failed to allocate bitmap buffer (%d bytes)\n", 
                   millis(), totalBitmapSize);
      free(displayImage);
      bmpFile.close();
      return nullptr;
    }
    
    // Read the entire bitmap data at once
    bmpFile.seek(header.dataOffset);
    size_t bytesRead = bmpFile.read(bmpData, totalBitmapSize);
    
    if (bytesRead != totalBitmapSize) {
      Serial.printf("[%lu] [SleepScreen] Warning: Read %d of %d bitmap bytes\n", 
                   millis(), bytesRead, totalBitmapSize);
    }
    
    // Source: bitmap data dimensions
    const int srcBytesPerRow = bmpRowSize;
    
    // Direct copy with rotation for 1bpp data
    // This is optimized to process an entire byte of source pixels at once
    const uint8_t bitMasks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}; // Precomputed bit masks
    
    // Process each source row
    for (int y = 0; y < height; y++) {
      // Calculate source row (BMPs are normally stored bottom-to-top)
      int srcRow = topDown ? y : (height - 1 - y);
      
      // In 90-degree rotation, source Y becomes destination X
      int destX = y;
      int destByteX = destX / 8;
      int destBitInByte = destX & 0x07; // Fast mod 8
      uint8_t destBitMask = bitMasks[destBitInByte]; 
      
      // Process chunks of 8 pixels at a time where possible
      for (int x = 0; x < width; x += 8) {
        // Get source byte containing 8 pixels
        int srcByteIdx = srcRow * srcBytesPerRow + (x >> 3); // Fast divide by 8
        uint8_t srcByte = bmpData[srcByteIdx];
        
        // Skip processing if byte is all white (0xFF in BMP means all white)
        if (srcByte == 0xFF) continue;
        
        // Limit to actual width if at the edge
        int pixelsInByte = min(8, width - x);
        
        // Process all bits in the source byte
        for (int bit = 0; bit < pixelsInByte; bit++) {
          // Source x-coordinate 
          int srcX = x + bit;
          
          // Get this bit from source (0=black, 1=white in BMP)
          // BMP format: bit 7 is leftmost pixel
          bool isBlack = ((srcByte & bitMasks[bit]) == 0);
          
          // Skip if white (optimization)
          if (!isBlack) continue;
          
          // Apply 90 degree clockwise rotation: (x,y) -> (y, width-1-x)
          int destY = width - 1 - srcX;
          
          // Calculate destination byte
          int destByteIdx = (destY * bytesPerRow) + destByteX;
          
          // For e-ink display: 0=black, 1=white
          // We initialized all to white (0xFF), so only need to set black pixels
          displayImage[destByteIdx] &= ~destBitMask;
        }
      }
    }
    
    // Clean up
    free(bmpData);
  } else {
    // Handle 24-bit BMPs with the existing pixel-by-pixel approach
    // Allocate buffer for reading BMP rows
    uint8_t* rowBuffer = (uint8_t*)malloc(bmpRowSize);
    if (!rowBuffer) {
      Serial.printf("[%lu] [SleepScreen] Failed to allocate row buffer\n", millis());
      free(displayImage);
      bmpFile.close();
      return nullptr;
    }

    // Process each row
    for (int y = 0; y < height; y++) {
      // Calculate source row (BMPs are normally stored bottom-to-top)
      int bmpRow = topDown ? y : (height - 1 - y);

      // Read one row from the BMP file
      bmpFile.seek(header.dataOffset + (bmpRow * bmpRowSize));
      bmpFile.read(rowBuffer, bmpRowSize);

      // Process each pixel in the row
      for (int x = 0; x < width; x++) {
        // For 24-bit BMPs, convert RGB to grayscale
        // BMP stores colors as BGR (Blue, Green, Red)
        int byteIndex = x * 3;
        uint8_t blue = rowBuffer[byteIndex];
        uint8_t green = rowBuffer[byteIndex + 1];
        uint8_t red = rowBuffer[byteIndex + 2];

        // Convert to grayscale using standard luminance formula
        uint8_t gray = (red * 30 + green * 59 + blue * 11) / 100;

        // If below threshold (128), consider it black
        bool isBlack = (gray < 128);

        // Apply 90 degree clockwise rotation
        int destX = y;
        int destY = width - 1 - x;

        // Calculate byte and bit position (1 bit per pixel)
        int destByteIndex = destY * bytesPerRow + (destX / 8);
        int destBitIndex = 7 - (destX % 8);  // MSB first (leftmost pixel in highest bit)

        // For e-ink display: 0=black, 1=white
        if (isBlack) {
          // Set to black (0) by clearing the corresponding bit
          displayImage[destByteIndex] &= ~(1 << destBitIndex);
        }
      }
    }
    
    // Clean up
    free(rowBuffer);
  }
  bmpFile.close();

  const unsigned long elapsedTime = millis() - startTime;
  Serial.printf("[%lu] [SleepScreen] Successfully loaded BMP: %dx%d in %lu ms\n", 
      millis(), destWidth, destHeight, elapsedTime);
  return displayImage;
}

void SleepScreen::onEnter() {
  const auto pageWidth = GfxRenderer::getScreenWidth();
  const auto pageHeight = GfxRenderer::getScreenHeight();

  renderer.clearScreen();

  // Try to load custom sleep image
  int imageWidth = 0;
  int imageHeight = 0;
  uint8_t* imageData = nullptr;

  // Try different possible paths
  const char* bmpPaths[] = {"sleep.bmp", "/sleep.bmp", "/SD/sleep.bmp"};

  // Try loading from different paths
  for (const char* path : bmpPaths) {
    imageData = loadBMP(path, imageWidth, imageHeight);
    if (imageData) {
        Serial.printf("[%lu] [SleepScreen] Successfully loaded: %s\n", millis(), path);
        break;
    }
  }

  if (imageData) {
    // Image loaded successfully
    Serial.printf("[%lu] [SleepScreen] Drawing image: %dx%d\n", millis(), imageWidth, imageHeight);

    // Calculate position to center the image
    int xPos = (pageWidth - imageWidth) / 2;
    int yPos = (pageHeight - imageHeight) / 2;
    if (xPos < 0) xPos = 0;
    if (yPos < 0) yPos = 0;

    // Draw the image - this sends the bitmap data to the e-ink display
    // Note: We've applied 90-degree clockwise rotation to compensate for
    // the renderer's behavior and ensure the image appears correctly
    // on the e-ink display.
    Serial.printf("[%lu] [SleepScreen] Drawing at position: %d,%d (dimensions: %dx%d)\n", millis(),
                 xPos, yPos, imageWidth, imageHeight);
    renderer.drawImage(imageData, xPos, yPos, imageWidth, imageHeight);

    // Free the image data
    free(imageData);
  } else {
    // Fall back to default image
    Serial.printf("[%lu] [SleepScreen] Failed to load sleep.bmp - using default image\n", millis());
    renderer.drawImage(CrossLarge, (pageWidth - 128) / 2, (pageHeight - 128) / 2, 128, 128);
    renderer.drawCenteredText(UI_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "SLEEPING");
  }

  // Apply white screen if enabled in settings
  if (!SETTINGS.whiteSleepScreen) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
}
