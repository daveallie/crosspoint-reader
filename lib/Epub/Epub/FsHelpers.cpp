#include "FsHelpers.h"

#include <SD.h>

bool FsHelpers::removeDir(const char* path) {
  // 1. Open the directory
  File dir = SD.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    return false;
  }

  File file = dir.openNextFile();
  while (file) {
    String filePath = path;
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    filePath += file.name();

    if (file.isDirectory()) {
      if (!removeDir(filePath.c_str())) {
        return false;
      }
    } else {
      if (!SD.remove(filePath.c_str())) {
        return false;
      }
    }
    file = dir.openNextFile();
  }

  return SD.rmdir(path);
}
