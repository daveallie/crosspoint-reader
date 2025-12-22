#include "SpineTocCache.h"

#include <HardwareSerial.h>
#include <SD.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <vector>

namespace {
constexpr uint8_t SPINE_TOC_CACHE_VERSION = 1;

// TODO: Centralize this?
std::string normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    components.push_back(component);
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}
}  // namespace

bool SpineTocCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;

  Serial.printf("[%lu] [STC] Beginning write to cache path: %s\n", millis(), cachePath.c_str());

  // Open spine file for writing
  const std::string spineFilePath = cachePath + "/spine.bin";
  Serial.printf("[%lu] [STC] Opening spine file: %s\n", millis(), spineFilePath.c_str());
  spineFile = SD.open(spineFilePath.c_str(), FILE_WRITE, true);
  if (!spineFile) {
    Serial.printf("[%lu] [STC] Failed to open spine file for writing: %s\n", millis(), spineFilePath.c_str());
    return false;
  }

  // Open TOC file for writing
  const std::string tocFilePath = cachePath + "/toc.bin";
  Serial.printf("[%lu] [STC] Opening toc file: %s\n", millis(), tocFilePath.c_str());
  tocFile = SD.open(tocFilePath.c_str(), FILE_WRITE, true);
  if (!tocFile) {
    Serial.printf("[%lu] [STC] Failed to open toc file for writing: %s\n", millis(), tocFilePath.c_str());
    spineFile.close();
    return false;
  }

  Serial.printf("[%lu] [STC] Began writing cache files\n", millis());
  return true;
}

void SpineTocCache::writeString(File& file, const std::string& s) const {
  const auto len = static_cast<uint32_t>(s.size());
  file.write(reinterpret_cast<const uint8_t*>(&len), sizeof(len));
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

void SpineTocCache::writeSpineEntry(File& file, const SpineEntry& entry) const {
  writeString(file, entry.href);
  file.write(reinterpret_cast<const uint8_t*>(&entry.cumulativeSize), sizeof(entry.cumulativeSize));
  file.write(reinterpret_cast<const uint8_t*>(&entry.tocIndex), sizeof(entry.tocIndex));
}

void SpineTocCache::writeTocEntry(File& file, const TocEntry& entry) const {
  writeString(file, entry.title);
  writeString(file, entry.href);
  writeString(file, entry.anchor);
  file.write(&entry.level, 1);
  file.write(reinterpret_cast<const uint8_t*>(&entry.spineIndex), sizeof(entry.spineIndex));
}

void SpineTocCache::addSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    Serial.printf("[%lu] [STC] addSpineEntry called but not in build mode\n", millis());
    return;
  }

  const SpineEntry entry(href, 0, -1);
  writeSpineEntry(spineFile, entry);
  spineCount++;
}

void SpineTocCache::addTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                const uint8_t level) {
  if (!buildMode || !tocFile) {
    Serial.printf("[%lu] [STC] addTocEntry called but not in build mode\n", millis());
    return;
  }

  const TocEntry entry(title, href, anchor, level, -1);
  writeTocEntry(tocFile, entry);
  tocCount++;
}

bool SpineTocCache::endWrite() {
  if (!buildMode) {
    Serial.printf("[%lu] [STC] endWrite called but not in build mode\n", millis());
    return false;
  }

  spineFile.close();
  tocFile.close();

  // Write metadata files with counts
  const auto spineMetaPath = cachePath + "/spine_meta.bin";
  File metaFile = SD.open(spineMetaPath.c_str(), FILE_WRITE, true);
  if (!metaFile) {
    Serial.printf("[%lu] [STC] Failed to write spine metadata\n", millis());
    return false;
  }
  metaFile.write(&SPINE_TOC_CACHE_VERSION, 1);
  metaFile.write(reinterpret_cast<const uint8_t*>(&spineCount), sizeof(spineCount));
  metaFile.write(reinterpret_cast<const uint8_t*>(&tocCount), sizeof(tocCount));
  metaFile.close();

  buildMode = false;
  Serial.printf("[%lu] [STC] Wrote %d spine, %d TOC entries\n", millis(), spineCount, tocCount);
  return true;
}

void SpineTocCache::readString(std::ifstream& is, std::string& s) const {
  uint32_t len;
  is.read(reinterpret_cast<char*>(&len), sizeof(len));
  s.resize(len);
  is.read(&s[0], len);
}

SpineTocCache::SpineEntry SpineTocCache::readSpineEntry(std::ifstream& is) const {
  SpineEntry entry;
  readString(is, entry.href);
  is.read(reinterpret_cast<char*>(&entry.cumulativeSize), sizeof(entry.cumulativeSize));
  is.read(reinterpret_cast<char*>(&entry.tocIndex), sizeof(entry.tocIndex));
  return entry;
}

SpineTocCache::TocEntry SpineTocCache::readTocEntry(std::ifstream& is) const {
  TocEntry entry;
  readString(is, entry.title);
  readString(is, entry.href);
  readString(is, entry.anchor);
  is.read(reinterpret_cast<char*>(&entry.level), 1);
  is.read(reinterpret_cast<char*>(&entry.spineIndex), sizeof(entry.spineIndex));
  return entry;
}

bool SpineTocCache::updateMappingsAndSizes(const std::string& epubPath) const {
  Serial.printf("[%lu] [STC] Computing mappings and sizes for %d spine, %d TOC entries\n", millis(), spineCount,
                tocCount);

  // Read all spine and TOC entries into temporary arrays (we need them all to compute mappings)
  // TODO: can we do this a bit smarter and avoid loading everything?
  std::vector<SpineEntry> spineEntries;
  std::vector<TocEntry> tocEntries;

  spineEntries.reserve(spineCount);
  tocEntries.reserve(tocCount);

  // Read spine entries
  {
    const auto spineFilePath = "/sd" + cachePath + "/spine.bin";
    std::ifstream spineStream(spineFilePath.c_str(), std::ios::binary);
    if (!spineStream) {
      Serial.printf("[%lu] [STC] Failed to open spine file for reading\n", millis());
      return false;
    }

    for (int i = 0; i < spineCount; i++) {
      spineEntries.push_back(readSpineEntry(spineStream));
    }
    spineStream.close();
  }

  // Read TOC entries
  {
    const auto tocFilePath = "/sd" + cachePath + "/toc.bin";
    std::ifstream tocStream(tocFilePath.c_str(), std::ios::binary);
    if (!tocStream) {
      Serial.printf("[%lu] [STC] Failed to open toc file for reading\n", millis());
      return false;
    }

    for (int i = 0; i < tocCount; i++) {
      tocEntries.push_back(readTocEntry(tocStream));
    }
    tocStream.close();
  }

  // Compute cumulative sizes
  const ZipFile zip("/sd" + epubPath);
  size_t cumSize = 0;

  for (int i = 0; i < spineCount; i++) {
    size_t itemSize = 0;
    const std::string path = normalisePath(spineEntries[i].href);
    if (zip.getInflatedFileSize(path.c_str(), &itemSize)) {
      cumSize += itemSize;
      spineEntries[i].cumulativeSize = cumSize;
    } else {
      Serial.printf("[%lu] [STC] Warning: Could not get size for spine item: %s\n", millis(), path.c_str());
    }
  }

  Serial.printf("[%lu] [STC] Book size: %lu\n", millis(), cumSize);

  // Compute spine → TOC mappings
  for (int i = 0; i < spineCount; i++) {
    for (int j = 0; j < tocCount; j++) {
      if (tocEntries[j].href == spineEntries[i].href) {
        spineEntries[i].tocIndex = static_cast<int16_t>(j);
        break;
      }
    }
  }

  // Compute TOC → spine mappings
  for (int i = 0; i < tocCount; i++) {
    for (int j = 0; j < spineCount; j++) {
      if (spineEntries[j].href == tocEntries[i].href) {
        tocEntries[i].spineIndex = static_cast<int16_t>(j);
        break;
      }
    }
  }

  // Rewrite spine file with updated data
  {
    const auto spineFilePath = cachePath + "/spine.bin";
    File spineFile = SD.open(spineFilePath.c_str(), FILE_WRITE, true);
    if (!spineFile) {
      Serial.printf("[%lu] [STC] Failed to reopen spine file for writing\n", millis());
      return false;
    }

    for (const auto& entry : spineEntries) {
      writeSpineEntry(spineFile, entry);
    }
    spineFile.close();
  }

  // Rewrite TOC file with updated data
  {
    const auto tocFilePath = cachePath + "/toc.bin";
    File tocFile = SD.open(tocFilePath.c_str(), FILE_WRITE, true);
    if (!tocFile) {
      Serial.printf("[%lu] [STC] Failed to reopen toc file for writing\n", millis());
      return false;
    }

    for (const auto& entry : tocEntries) {
      writeTocEntry(tocFile, entry);
    }
    tocFile.close();
  }

  // Clear vectors to free memory
  spineEntries.clear();
  spineEntries.shrink_to_fit();
  tocEntries.clear();
  tocEntries.shrink_to_fit();

  Serial.printf("[%lu] [STC] Updated cache with mappings and sizes\n", millis());
  return true;
}

bool SpineTocCache::load() {
  // Load metadata
  const auto metaPath = cachePath + "/spine_meta.bin";
  if (!SD.exists(metaPath.c_str())) {
    Serial.printf("[%lu] [STC] Cache metadata does not exist: %s\n", millis(), metaPath.c_str());
    return false;
  }

  File metaFile = SD.open(metaPath.c_str(), FILE_READ);
  if (!metaFile) {
    Serial.printf("[%lu] [STC] Failed to open cache metadata\n", millis());
    return false;
  }

  uint8_t version;
  metaFile.read(&version, 1);
  if (version != SPINE_TOC_CACHE_VERSION) {
    Serial.printf("[%lu] [STC] Cache version mismatch: expected %d, got %d\n", millis(), SPINE_TOC_CACHE_VERSION,
                  version);
    metaFile.close();
    return false;
  }

  metaFile.read(reinterpret_cast<uint8_t*>(&spineCount), sizeof(spineCount));
  metaFile.read(reinterpret_cast<uint8_t*>(&tocCount), sizeof(tocCount));
  metaFile.close();

  loaded = true;
  Serial.printf("[%lu] [STC] Loaded cache metadata: %d spine, %d TOC entries\n", millis(), spineCount, tocCount);
  return true;
}

SpineTocCache::SpineEntry SpineTocCache::getSpineEntry(const int index) const {
  if (!loaded) {
    Serial.printf("[%lu] [STC] getSpineEntry called but cache not loaded\n", millis());
    return SpineEntry();
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    Serial.printf("[%lu] [STC] getSpineEntry index %d out of range\n", millis(), index);
    return SpineEntry();
  }

  const auto spineFilePath = "/sd" + cachePath + "/spine.bin";
  std::ifstream spineStream(spineFilePath.c_str(), std::ios::binary);
  if (!spineStream) {
    Serial.printf("[%lu] [STC] Failed to open spine file for reading entry\n", millis());
    return SpineEntry();
  }

  // Seek to the correct entry - need to read entries sequentially until we reach the index
  // TODO: This could/should be based on a look up table/fixed sizes
  for (int i = 0; i < index; i++) {
    readSpineEntry(spineStream);  // Skip entries
  }

  auto entry = readSpineEntry(spineStream);
  spineStream.close();
  return entry;
}

SpineTocCache::TocEntry SpineTocCache::getTocEntry(const int index) const {
  if (!loaded) {
    Serial.printf("[%lu] [STC] getTocEntry called but cache not loaded\n", millis());
    return TocEntry();
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    Serial.printf("[%lu] [STC] getTocEntry index %d out of range\n", millis(), index);
    return TocEntry();
  }

  const auto tocFilePath = "/sd" + cachePath + "/toc.bin";
  std::ifstream tocStream(tocFilePath.c_str(), std::ios::binary);
  if (!tocStream) {
    Serial.printf("[%lu] [STC] Failed to open toc file for reading entry\n", millis());
    return TocEntry();
  }

  // Seek to the correct entry - need to read entries sequentially until we reach the index
  // TODO: This could/should be based on a look up table/fixed sizes
  for (int i = 0; i < index; i++) {
    readTocEntry(tocStream);  // Skip entries
  }

  auto entry = readTocEntry(tocStream);
  tocStream.close();
  return entry;
}

int SpineTocCache::getSpineCount() const { return spineCount; }

int SpineTocCache::getTocCount() const { return tocCount; }

bool SpineTocCache::isLoaded() const { return loaded; }
