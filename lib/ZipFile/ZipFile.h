#pragma once
#include <Print.h>

#include <memory>
#include <string>

#include "miniz.h"

class ZipFile {
  std::string filePath;
  std::unique_ptr<mz_zip_archive> zipArchivePtr;
  bool loadFileStat(const char* filename, mz_zip_archive_file_stat* fileStat);
  long getDataOffset(const mz_zip_archive_file_stat& fileStat) const;

 public:
  explicit ZipFile(std::string filePath) : filePath(std::move(filePath)) {}
  ~ZipFile() = default;
  // Zip file can be opened and closed by hand in order to allow for quick calculation of inflated file size
  // It is NOT recommended to pre-open it for any kind of inflation due to memory constraints
  bool isOpen() const { return zipArchivePtr != nullptr; }
  bool open();
  bool close();
  bool getInflatedFileSize(const char* filename, size_t* size);
  // Due to the memory required to run each of these, it is recommended to not preopen the zip file for multiple
  // These functions will open and close the zip as needed
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize);
};
