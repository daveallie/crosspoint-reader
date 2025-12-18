#pragma once
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "FootnoteEntry.h"
#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
};

class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual void render(GfxRenderer& renderer, int fontId) = 0;
  virtual void serialize(std::ostream& os) = 0;
};

class PageLine final : public PageElement {
  std::shared_ptr<TextBlock> block;

 public:
  PageLine(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}
  void render(GfxRenderer& renderer, int fontId) override;
  void serialize(std::ostream& os) override;
  static std::unique_ptr<PageLine> deserialize(std::istream& is);
};

class Page {
 private:
  std::shared_ptr<PageElement>* elements;
  int elementCapacity;

  FootnoteEntry* footnotes;
  int footnoteCapacity;

 public:
  int elementCount;
  int footnoteCount;

  Page() : elementCount(0), footnoteCount(0) {
    elementCapacity = 24;
    elements = new std::shared_ptr<PageElement>[elementCapacity];

    footnoteCapacity = 16;
    footnotes = new FootnoteEntry[footnoteCapacity];
    for (int i = 0; i < footnoteCapacity; i++) {
      footnotes[i].number[0] = '\0';
      footnotes[i].href[0] = '\0';
    }
  }

  ~Page() {
    delete[] elements;
    delete[] footnotes;
  }

  Page(const Page&) = delete;
  Page& operator=(const Page&) = delete;

  void addElement(std::shared_ptr<PageElement> element) {
    if (elementCount < elementCapacity) {
      elements[elementCount++] = element;
    }
  }

  void addFootnote(const char* number, const char* href) {
    if (footnoteCount < footnoteCapacity) {
      strncpy(footnotes[footnoteCount].number, number, 2);
      footnotes[footnoteCount].number[2] = '\0';
      strncpy(footnotes[footnoteCount].href, href, 63);
      footnotes[footnoteCount].href[63] = '\0';
      footnoteCount++;
    }
  }

  std::shared_ptr<PageElement> getElement(int index) const {
    if (index >= 0 && index < elementCount) {
      return elements[index];
    }
    return nullptr;
  }

  FootnoteEntry* getFootnote(int index) {
    if (index >= 0 && index < footnoteCount) {
      return &footnotes[index];
    }
    return nullptr;
  }

  void render(GfxRenderer& renderer, int fontId) const;
  void serialize(std::ostream& os) const;
  static std::unique_ptr<Page> deserialize(std::istream& is);
};