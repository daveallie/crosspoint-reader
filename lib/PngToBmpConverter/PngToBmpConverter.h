#pragma once

#include <Print.h>
#include <SdFat.h>

class PngToBmpConverter {
 public:
  // Convert PNG file to 2-bit BMP stream
  // Similar API to JpegToBmpConverter for consistency
  static bool pngFileToBmpStream(FsFile& pngFile, Print& bmpOut);
};
