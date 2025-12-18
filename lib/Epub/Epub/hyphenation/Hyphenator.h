#pragma once

#include <EpdFontFamily.h>

#include <string>

class GfxRenderer;

// Holds the split portions of a hyphenated word.
struct HyphenationResult {
  std::string head;
  std::string tail;
};

class Hyphenator {
 public:
  // Splits a word so it fits within availableWidth, appending a hyphen to the head when needed.
  static bool splitWord(const GfxRenderer& renderer, int fontId, const std::string& word, EpdFontStyle style,
                        int availableWidth, HyphenationResult* result, bool force);
};