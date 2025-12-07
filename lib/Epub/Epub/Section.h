#pragma once
#include "Epub.h"

class Page;
class EpdRenderer;

class Section {
  Epub* epub;
  const int spineIndex;
  EpdRenderer& renderer;
  std::string cachePath;

  void onPageComplete(const Page* page);

 public:
  int pageCount = 0;
  int currentPage = 0;

  explicit Section(Epub* epub, const int spineIndex, EpdRenderer& renderer)
      : epub(epub), spineIndex(spineIndex), renderer(renderer) {
    cachePath = epub->getCachePath() + "/" + std::to_string(spineIndex);
  }
  ~Section() = default;
  void writeCacheMetadata() const;
  bool loadCacheMetadata();
  void setupCacheDir() const;
  void clearCache() const;
  bool persistPageDataToSD();
  Page* loadPageFromSD() const;
};
