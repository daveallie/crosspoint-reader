#include "SpineTocCache.h"

#include <HardwareSerial.h>
#include <SD.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <vector>

#include "FsHelpers.h"

namespace {
constexpr uint8_t SPINE_TOC_CACHE_VERSION = 1;
constexpr size_t SPINE_TOC_META_HEADER_SIZE = sizeof(SPINE_TOC_CACHE_VERSION) + sizeof(uint16_t) * 2;
constexpr char spineTocMetaBinFile[] = "/spine_toc_meta.bin";
constexpr char spineBinFile[] = "/spine.bin";
constexpr char tocBinFile[] = "/toc.bin";
}  // namespace

bool SpineTocCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;

  Serial.printf("[%lu] [STC] Beginning write to cache path: %s\n", millis(), cachePath.c_str());

  // Open spine file for writing
  if (!FsHelpers::openFileForWrite("STC", cachePath + spineBinFile, spineFile)) {
    return false;
  }

  // Open TOC file for writing
  if (!FsHelpers::openFileForWrite("STC", cachePath + tocBinFile, tocFile)) {
    spineFile.close();
    return false;
  }

  // Open meta file for writing
  if (!FsHelpers::openFileForWrite("STC", cachePath + spineTocMetaBinFile, metaFile)) {
    spineFile.close();
    tocFile.close();
    return false;
  }

  // Write 0s into first slots, LUT is written during `addSpineEntry` and `addTocEntry`, and counts are rewritten at
  // the end
  serialization::writePod(metaFile, SPINE_TOC_CACHE_VERSION);
  serialization::writePod(metaFile, spineCount);
  serialization::writePod(metaFile, tocCount);

  Serial.printf("[%lu] [STC] Began writing cache files\n", millis());
  return true;
}

size_t SpineTocCache::writeSpineEntry(File& file, const SpineEntry& entry) const {
  const auto pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

size_t SpineTocCache::writeTocEntry(File& file, const TocEntry& entry) const {
  const auto pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void SpineTocCache::addSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile || !metaFile) {
    Serial.printf("[%lu] [STC] addSpineEntry called but not in build mode\n", millis());
    return;
  }

  const SpineEntry entry(href, 0, -1);
  const auto position = writeSpineEntry(spineFile, entry);
  serialization::writePod(metaFile, position);
  spineCount++;
}

void SpineTocCache::addTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                const uint8_t level) {
  if (!buildMode || !tocFile || !metaFile) {
    Serial.printf("[%lu] [STC] addTocEntry called but not in build mode\n", millis());
    return;
  }

  const TocEntry entry(title, href, anchor, level, -1);
  const auto position = writeTocEntry(tocFile, entry);
  serialization::writePod(metaFile, position);
  tocCount++;
}

bool SpineTocCache::endWrite() {
  if (!buildMode) {
    Serial.printf("[%lu] [STC] endWrite called but not in build mode\n", millis());
    return false;
  }

  spineFile.close();
  tocFile.close();

  // Write correct counts into meta file
  metaFile.seek(sizeof(SPINE_TOC_CACHE_VERSION));
  serialization::writePod(metaFile, spineCount);
  serialization::writePod(metaFile, tocCount);
  metaFile.close();

  buildMode = false;
  Serial.printf("[%lu] [STC] Wrote %d spine, %d TOC entries\n", millis(), spineCount, tocCount);
  return true;
}

SpineTocCache::SpineEntry SpineTocCache::readSpineEntry(File& file) const {
  SpineEntry entry;
  serialization::readString(file, entry.href);
  serialization::readPod(file, entry.cumulativeSize);
  serialization::readPod(file, entry.tocIndex);
  return entry;
}

SpineTocCache::TocEntry SpineTocCache::readTocEntry(File& file) const {
  TocEntry entry;
  serialization::readString(file, entry.title);
  serialization::readString(file, entry.href);
  serialization::readString(file, entry.anchor);
  serialization::readPod(file, entry.level);
  serialization::readPod(file, entry.spineIndex);
  return entry;
}

bool SpineTocCache::updateMapsAndSizes(const std::string& epubPath) {
  Serial.printf("[%lu] [STC] Computing mappings and sizes for %d spine, %d TOC entries\n", millis(), spineCount,
                tocCount);

  std::vector<SpineEntry> spineEntries;
  spineEntries.reserve(spineCount);

  // Load only the spine items, update them in memory while loading one TOC at a time and storing it
  {
    if (!FsHelpers::openFileForRead("STC", cachePath + spineBinFile, spineFile)) {
      return false;
    }
    for (int i = 0; i < spineCount; i++) {
      spineEntries.push_back(readSpineEntry(spineFile));
    }
    spineFile.close();
  }

  // Iterate over TOC entries and update them with the spine mapping
  // We do this by moving the TOC file and then making a new one parsing through both at the same time
  {
    SD.rename((cachePath + tocBinFile).c_str(), (cachePath + tocBinFile + ".tmp").c_str());
    File tempTocFile;
    if (!FsHelpers::openFileForRead("STC", cachePath + tocBinFile + ".tmp", tempTocFile)) {
      SD.remove((cachePath + tocBinFile + ".tmp").c_str());
      return false;
    }
    if (!FsHelpers::openFileForWrite("STC", cachePath + tocBinFile, tocFile)) {
      tempTocFile.close();
      SD.remove((cachePath + tocBinFile + ".tmp").c_str());
      return false;
    }

    for (int i = 0; i < tocCount; i++) {
      auto tocEntry = readTocEntry(tempTocFile);

      // Find the matching spine entry
      for (int j = 0; j < spineCount; j++) {
        if (spineEntries[j].href == tocEntry.href) {
          tocEntry.spineIndex = static_cast<int16_t>(j);
          // Point the spine to the first TOC entry we come across (in the case that there are multiple)
          if (spineEntries[j].tocIndex == -1) spineEntries[j].tocIndex = static_cast<int16_t>(i);
          break;
        }
      }

      writeTocEntry(tocFile, tocEntry);
    }
    tocFile.close();
    tempTocFile.close();
    SD.remove((cachePath + tocBinFile + ".tmp").c_str());
  }

  // By this point all the spine items in memory should have the right `tocIndex` and the TOC file is complete

  // Next, compute cumulative sizes
  {
    const ZipFile zip("/sd" + epubPath);
    size_t cumSize = 0;

    for (int i = 0; i < spineCount; i++) {
      size_t itemSize = 0;
      const std::string path = FsHelpers::normalisePath(spineEntries[i].href);
      if (zip.getInflatedFileSize(path.c_str(), &itemSize)) {
        cumSize += itemSize;
        spineEntries[i].cumulativeSize = cumSize;
      } else {
        Serial.printf("[%lu] [STC] Warning: Could not get size for spine item: %s\n", millis(), path.c_str());
      }
    }

    Serial.printf("[%lu] [STC] Book size: %lu\n", millis(), cumSize);
  }

  // Rewrite spine file with updated data
  {
    if (!FsHelpers::openFileForWrite("STC", cachePath + spineBinFile, spineFile)) {
      // metaFile.close();
      return false;
    }
    for (const auto& entry : spineEntries) {
      writeSpineEntry(spineFile, entry);
    }
    spineFile.close();
  }

  // Clear vectors to free memory
  spineEntries.clear();
  spineEntries.shrink_to_fit();

  Serial.printf("[%lu] [STC] Updated cache with mappings and sizes\n", millis());
  return true;
}

// Opens (and leaves open all three files for fast access)
bool SpineTocCache::load() {
  // Load metadata
  if (!FsHelpers::openFileForRead("STC", cachePath + spineTocMetaBinFile, metaFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(metaFile, version);
  if (version != SPINE_TOC_CACHE_VERSION) {
    Serial.printf("[%lu] [STC] Cache version mismatch: expected %d, got %d\n", millis(), SPINE_TOC_CACHE_VERSION,
                  version);
    metaFile.close();
    return false;
  }

  if (!FsHelpers::openFileForRead("STC", cachePath + spineBinFile, spineFile)) {
    metaFile.close();
    return false;
  }

  if (!FsHelpers::openFileForRead("STC", cachePath + tocBinFile, tocFile)) {
    metaFile.close();
    spineFile.close();
    return false;
  }

  serialization::readPod(metaFile, spineCount);
  serialization::readPod(metaFile, tocCount);

  loaded = true;
  Serial.printf("[%lu] [STC] Loaded cache metadata: %d spine, %d TOC entries\n", millis(), spineCount, tocCount);
  return true;
}

SpineTocCache::SpineEntry SpineTocCache::getSpineEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [STC] getSpineEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    Serial.printf("[%lu] [STC] getSpineEntry index %d out of range\n", millis(), index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  metaFile.seek(SPINE_TOC_META_HEADER_SIZE + sizeof(size_t) * index);
  size_t spineEntryPos;
  serialization::readPod(metaFile, spineEntryPos);
  spineFile.seek(spineEntryPos);
  auto entry = readSpineEntry(spineFile);
  return entry;
}

SpineTocCache::TocEntry SpineTocCache::getTocEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [STC] getTocEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    Serial.printf("[%lu] [STC] getTocEntry index %d out of range\n", millis(), index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  metaFile.seek(SPINE_TOC_META_HEADER_SIZE + sizeof(size_t) * spineCount + sizeof(size_t) * index);
  size_t tocEntryPos;
  serialization::readPod(metaFile, tocEntryPos);
  tocFile.seek(tocEntryPos);
  auto entry = readTocEntry(tocFile);
  return entry;
}

int SpineTocCache::getSpineCount() const { return spineCount; }

int SpineTocCache::getTocCount() const { return tocCount; }

bool SpineTocCache::isLoaded() const { return loaded; }
