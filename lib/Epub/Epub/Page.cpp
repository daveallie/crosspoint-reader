#include "Page.h"

#include <HardwareSerial.h>
#include <Serialization.h>

namespace {
constexpr uint8_t PAGE_FILE_VERSION = 3;
}

void PageLine::render(GfxRenderer& renderer, const int fontId) { block->render(renderer, fontId, xPos, yPos); }

void PageLine::serialize(std::ostream& os) {
  serialization::writePod(os, xPos);
  serialization::writePod(os, yPos);
  block->serialize(os);
}

std::unique_ptr<PageLine> PageLine::deserialize(std::istream& is) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(is, xPos);
  serialization::readPod(is, yPos);

  auto tb = TextBlock::deserialize(is);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId) const {
  for (int i = 0; i < elementCount; i++) {
    elements[i]->render(renderer, fontId);
  }
}

void Page::serialize(std::ostream& os) const {
  serialization::writePod(os, PAGE_FILE_VERSION);
  serialization::writePod(os, static_cast<uint32_t>(elementCount));

  for (int i = 0; i < elementCount; i++) {
    serialization::writePod(os, static_cast<uint8_t>(TAG_PageLine));
    elements[i]->serialize(os);
  }

  serialization::writePod(os, static_cast<int32_t>(footnoteCount));
  for (int i = 0; i < footnoteCount; i++) {
    os.write(footnotes[i].number, 3);
    os.write(footnotes[i].href, 64);
    uint8_t isInlineFlag = footnotes[i].isInline ? 1 : 0;
    os.write(reinterpret_cast<const char*>(&isInlineFlag), 1);
  }
}

std::unique_ptr<Page> Page::deserialize(std::istream& is) {
  uint8_t version;
  serialization::readPod(is, version);
  if (version != PAGE_FILE_VERSION) {
    Serial.printf("[%lu] [PGE] Deserialization failed: Unknown version %u\n", millis(), version);
    return nullptr;
  }

  auto page = std::unique_ptr<Page>(new Page());

  uint32_t count;
  serialization::readPod(is, count);

  for (uint32_t i = 0; i < count && i < page->elementCapacity; i++) {
    uint8_t tag;
    serialization::readPod(is, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(is);
      page->addElement(std::move(pl));
    } else {
      Serial.printf("[%lu] [PGE] Deserialization failed: Unknown tag %u\n", millis(), tag);
      return nullptr;
    }
  }

  int32_t footnoteCount;
  serialization::readPod(is, footnoteCount);
  page->footnoteCount = (footnoteCount < page->footnoteCapacity) ? footnoteCount : page->footnoteCapacity;

  for (int i = 0; i < page->footnoteCount; i++) {
    is.read(page->footnotes[i].number, 3);
    is.read(page->footnotes[i].href, 64);
    uint8_t isInlineFlag = 0;
    is.read(reinterpret_cast<char*>(&isInlineFlag), 1);
    page->footnotes[i].isInline = (isInlineFlag != 0);
  }

  return page;
}
