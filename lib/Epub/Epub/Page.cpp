#include "Page.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  FsFile bmpFile;
  if (!SdMan.openFileForRead("PGI", cachedBmpPath, bmpFile)) {
    Serial.printf("[%lu] [PGI] Failed to open cached BMP: %s\n", millis(), cachedBmpPath.c_str());
    return;
  }

  Bitmap bitmap(bmpFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    Serial.printf("[%lu] [PGI] Failed to parse BMP headers\n", millis());
    bmpFile.close();
    return;
  }

  // Calculate viewport dimensions (480x800 portrait)
  const int viewportWidth = 480;
  const int viewportHeight = 800;

  // Render centered on screen, ignoring text margins
  // Images should fill the screen, not respect text padding
  renderer.drawBitmap(bitmap, 0, 0, viewportWidth, viewportHeight);
  bmpFile.close();
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writeString(file, cachedBmpPath);
  serialization::writePod(file, imageWidth);
  serialization::writePod(file, imageHeight);
  return true;
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos, yPos;
  uint16_t imageWidth, imageHeight;
  std::string cachedBmpPath;

  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readString(file, cachedBmpPath);
  serialization::readPod(file, imageWidth);
  serialization::readPod(file, imageHeight);

  return std::unique_ptr<PageImage>(new PageImage(std::move(cachedBmpPath), imageWidth, imageHeight, xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset);
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    // Get element type tag via virtual function
    const PageElementTag tag = el->getTag();
    serialization::writePod(file, static_cast<uint8_t>(tag));
    if (!el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      page->elements.push_back(std::move(pi));
    } else {
      Serial.printf("[%lu] [PGE] Deserialization failed: Unknown tag %u\n", millis(), tag);
      return nullptr;
    }
  }

  return page;
}
