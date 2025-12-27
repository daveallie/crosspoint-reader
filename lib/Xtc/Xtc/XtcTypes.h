/**
 * XtcTypes.h
 *
 * XTC file format type definitions
 * XTC ebook support for CrossPoint Reader
 *
 * XTC is the native binary ebook format for XTeink X4 e-reader.
 * It stores pre-rendered bitmap images per page.
 *
 * Format based on EPUB2XTC converter by Rafal-P-Mazur
 */

#pragma once

#include <cstdint>

namespace xtc {

// XTC file magic numbers (little-endian)
// "XTC\0" = 0x58, 0x54, 0x43, 0x00
constexpr uint32_t XTC_MAGIC = 0x00435458;  // "XTC\0" in little-endian
// "XTG\0" = 0x58, 0x54, 0x47, 0x00
constexpr uint32_t XTG_MAGIC = 0x00475458;  // "XTG\0" for page data

// XTeink X4 display resolution
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 800;

// XTC file header (56 bytes)
#pragma pack(push, 1)
struct XtcHeader {
  uint32_t magic;            // 0x00: Magic number "XTC\0" (0x00435458)
  uint16_t version;          // 0x04: Format version (typically 1)
  uint16_t pageCount;        // 0x06: Total page count
  uint32_t flags;            // 0x08: Flags/reserved
  uint32_t headerSize;       // 0x0C: Size of header section (typically 88)
  uint32_t reserved1;        // 0x10: Reserved
  uint32_t tocOffset;        // 0x14: TOC offset (0 if unused) - 4 bytes, not 8!
  uint64_t pageTableOffset;  // 0x18: Page table offset
  uint64_t dataOffset;       // 0x20: First page data offset
  uint64_t reserved2;        // 0x28: Reserved
  uint32_t titleOffset;      // 0x30: Title string offset
  uint32_t padding;          // 0x34: Padding to 56 bytes
};
#pragma pack(pop)

// Page table entry (16 bytes per page)
#pragma pack(push, 1)
struct PageTableEntry {
  uint64_t dataOffset;  // 0x00: Absolute offset to page data
  uint32_t dataSize;    // 0x08: Page data size in bytes
  uint16_t width;       // 0x0C: Page width (480)
  uint16_t height;      // 0x0E: Page height (800)
};
#pragma pack(pop)

// XTG page data header (22 bytes)
#pragma pack(push, 1)
struct XtgPageHeader {
  uint32_t magic;       // 0x00: Magic "XTG\0" (0x00475458)
  uint16_t width;       // 0x04: Bitmap width
  uint16_t height;      // 0x06: Bitmap height
  uint16_t reserved1;   // 0x08: Reserved (0)
  uint32_t bitmapSize;  // 0x0A: Bitmap data size = ((width+7)/8) * height
  uint32_t reserved2;   // 0x0E: Reserved (0)
  uint32_t reserved3;   // 0x12: Reserved (0)
  // Followed by bitmap data at offset 0x16 (22)
};
#pragma pack(pop)

// Page information (internal use)
struct PageInfo {
  uint64_t offset;  // File offset to page data
  uint32_t size;    // Data size (bytes)
  uint16_t width;   // Page width
  uint16_t height;  // Page height
};

// Error codes
enum class XtcError {
  OK = 0,
  FILE_NOT_FOUND,
  INVALID_MAGIC,
  INVALID_VERSION,
  CORRUPTED_HEADER,
  PAGE_OUT_OF_RANGE,
  READ_ERROR,
  WRITE_ERROR,
  MEMORY_ERROR,
  DECOMPRESSION_ERROR,
};

// Convert error code to string
inline const char* errorToString(XtcError err) {
  switch (err) {
    case XtcError::OK:
      return "OK";
    case XtcError::FILE_NOT_FOUND:
      return "File not found";
    case XtcError::INVALID_MAGIC:
      return "Invalid magic number";
    case XtcError::INVALID_VERSION:
      return "Unsupported version";
    case XtcError::CORRUPTED_HEADER:
      return "Corrupted header";
    case XtcError::PAGE_OUT_OF_RANGE:
      return "Page out of range";
    case XtcError::READ_ERROR:
      return "Read error";
    case XtcError::WRITE_ERROR:
      return "Write error";
    case XtcError::MEMORY_ERROR:
      return "Memory allocation error";
    case XtcError::DECOMPRESSION_ERROR:
      return "Decompression error";
    default:
      return "Unknown error";
  }
}

/**
 * Check if filename has XTC extension
 */
inline bool isXtcExtension(const char* filename) {
  if (!filename) return false;
  const char* ext = strrchr(filename, '.');
  if (!ext) return false;
  return (strcasecmp(ext, ".xtc") == 0 || strcasecmp(ext, ".xtg") == 0 || strcasecmp(ext, ".xth") == 0);
}

}  // namespace xtc
