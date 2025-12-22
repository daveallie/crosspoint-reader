#pragma once
#include <Print.h>

#include "Epub.h"
#include "Epub/SpineTocCache.h"
#include "expat.h"

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_MANIFEST,
    IN_SPINE,
  };

  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  SpineTocCache* cache;
  File tempItemStore;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::string title;
  std::string tocNcxPath;
  std::string coverItemId;

  explicit ContentOpfParser(const std::string& baseContentPath, const size_t xmlSize, SpineTocCache* cache)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
