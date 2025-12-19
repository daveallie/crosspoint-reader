#pragma once
#include <HardwareSerial.h>
#include <Print.h>

#include <cstddef>
#include <functional>
#include <string>

#include "miniz.h"

class ZipFile {
  std::string filePath;
  mutable mz_zip_archive zipArchive = {};
  bool loadFileStat(const char* filename, mz_zip_archive_file_stat* fileStat) const;
  long getDataOffset(const mz_zip_archive_file_stat& fileStat) const;

 public:
  explicit ZipFile(std::string filePath) : filePath(std::move(filePath)) {
    const bool status = mz_zip_reader_init_file(&zipArchive, this->filePath.c_str(), 0);

    if (!status) {
      Serial.printf("[%lu] [ZIP] mz_zip_reader_init_file() failed for %s! Error: %s\n", millis(),
                    this->filePath.c_str(), mz_zip_get_error_string(zipArchive.m_last_error));
    }
  }
  ~ZipFile() { mz_zip_reader_end(&zipArchive); }
  bool getInflatedFileSize(const char* filename, size_t* size) const;
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false) const;
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize) const;
};
