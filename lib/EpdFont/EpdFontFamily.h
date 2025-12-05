#pragma once
#include "EpdFont.h"

enum EpdFontStyle { REGULAR, BOLD, ITALIC, BOLD_ITALIC };

class EpdFontFamily {
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;

  const EpdFont* getFont(EpdFontStyle style) const;

 public:
  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style = REGULAR) const;
  bool hasPrintableChars(const char* string, EpdFontStyle style = REGULAR) const;

  const EpdFontData* getData(EpdFontStyle style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const;
};
