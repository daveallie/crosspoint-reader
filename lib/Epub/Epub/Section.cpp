#include "Section.h"

#include <EpdRenderer.h>
#include <SD.h>

#include <fstream>

#include "EpubHtmlParserSlim.h"
#include "Page.h"
#include "Serialization.h"

constexpr uint8_t SECTION_FILE_VERSION = 2;

void Section::onPageComplete(const Page* page) {
  Serial.printf("Page %d complete - free mem: %lu\n", pageCount, ESP.getFreeHeap());

  const auto filePath = cachePath + "/page_" + std::to_string(pageCount) + ".bin";

  std::ofstream outputFile("/sd" + filePath);
  page->serialize(outputFile);
  outputFile.close();

  pageCount++;
  delete page;
}

void Section::writeCacheMetadata() const {
  std::ofstream outputFile(("/sd" + cachePath + "/section.bin").c_str());
  serialization::writePod(outputFile, SECTION_FILE_VERSION);
  serialization::writePod(outputFile, pageCount);
  outputFile.close();
}

bool Section::loadCacheMetadata() {
  if (!SD.exists(cachePath.c_str())) {
    return false;
  }

  const auto sectionFilePath = cachePath + "/section.bin";
  if (!SD.exists(sectionFilePath.c_str())) {
    return false;
  }

  std::ifstream inputFile(("/sd" + sectionFilePath).c_str());
  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != SECTION_FILE_VERSION) {
    inputFile.close();
    SD.remove(sectionFilePath.c_str());
    Serial.printf("Section state file: Unknown version %u\n", version);
    return false;
  }
  serialization::readPod(inputFile, pageCount);
  inputFile.close();
  Serial.printf("Loaded cache: %d pages\n", pageCount);
  return true;
}

void Section::setupCacheDir() const {
  epub->setupCacheDir();
  SD.mkdir(cachePath.c_str());
}

void Section::clearCache() const { SD.rmdir(cachePath.c_str()); }

bool Section::persistPageDataToSD() {
  const auto localPath = epub->getSpineItem(spineIndex);

  // TODO: Should we get rid of this file all together?
  //       It currently saves us a bit of memory by allowing for all the inflation bits to be released
  //       before loading the XML parser
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";
  File f = SD.open(tmpHtmlPath.c_str(), FILE_WRITE, true);
  bool success = epub->readItemContentsToStream(localPath, f, 1024);
  f.close();

  if (!success) {
    Serial.println("Failed to stream item contents");
    return false;
  }

  Serial.printf("Streamed HTML to %s\n", tmpHtmlPath.c_str());

  const auto sdTmpHtmlPath = "/sd" + tmpHtmlPath;

  auto visitor =
      EpubHtmlParserSlim(sdTmpHtmlPath.c_str(), renderer, [this](const Page* page) { this->onPageComplete(page); });
  success = visitor.parseAndBuildPages();

  SD.remove(tmpHtmlPath.c_str());
  if (!success) {
    Serial.println("Failed to parse and build pages");
    return false;
  }

  writeCacheMetadata();

  return true;
}

void Section::renderPage() const {
  if (0 <= currentPage && currentPage < pageCount) {
    const auto filePath = "/sd" + cachePath + "/page_" + std::to_string(currentPage) + ".bin";
    std::ifstream inputFile(filePath);
    const Page* p = Page::deserialize(inputFile);
    inputFile.close();
    p->render(renderer);
    delete p;
  } else if (pageCount == 0) {
    Serial.println("No pages to render");
    const int width = renderer.getTextWidth("Empty chapter", BOLD);
    renderer.drawText((renderer.getPageWidth() - width) / 2, 300, "Empty chapter", 1, BOLD);
  } else {
    Serial.printf("Page out of bounds: %d (max %d)\n", currentPage, pageCount);
    const int width = renderer.getTextWidth("Out of bounds", BOLD);
    renderer.drawText((renderer.getPageWidth() - width) / 2, 300, "Out of bounds", 1, BOLD);
  }
}
