#pragma once
#include <Print.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Epub/EpubTocEntry.h"

class ZipFile;

class Epub {
  std::string title;
  std::string coverImageItem;
  std::string tocNcxItem;
  std::string filepath;
  std::vector<std::pair<std::string, std::string>> spine;
  // the file size of the spine items (proxy to book progress)
  std::vector<size_t> cumulativeSpineItemSize;
  // the toc of the EPUB file
  std::vector<EpubTocEntry> toc;
  std::string contentBasePath;
  std::string cachePath;

  // Use pointers, allocate only if needed
  std::unordered_set<std::string>* footnotePages;
  std::vector<std::string>* virtualSpineItems;

  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(const std::string& contentOpfFilePath);
  bool parseTocNcxFile();
  void initializeSpineItemSizes();

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir)
      : filepath(std::move(filepath)), footnotePages(nullptr), virtualSpineItems(nullptr) {
    cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
  }

  ~Epub() {
    delete footnotePages;
    delete virtualSpineItems;
  }

  std::string& getBasePath() { return contentBasePath; }
  bool load();
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getCoverImageItem() const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  std::string getSpineItem(int index) const;
  int getSpineItemsCount() const;
  size_t getCumulativeSpineItemSize(const int spineIndex) const;
  EpubTocEntry& getTocItem(int tocIndex);
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;

  void markAsFootnotePage(const std::string& href);
  bool isFootnotePage(const std::string& filename) const;
  bool shouldHideFromToc(int spineIndex) const;
  int addVirtualSpineItem(const std::string& path);
  bool isVirtualSpineItem(int spineIndex) const;
  int findVirtualSpineIndex(const std::string& filename) const;

  size_t getBookSize() const;
  uint8_t calculateProgress(const int currentSpineIndex, const float currentSpineRead);
};
