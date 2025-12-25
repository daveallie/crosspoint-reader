#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
 public:
  using PrefixWidthCache = std::unordered_map<const void*, std::unordered_map<size_t, uint16_t>>;

  explicit ParsedText(const TextBlock::BLOCK_STYLE style, bool extraParagraphSpacing, bool hyphenationEnabled)
      : style(style), extraParagraphSpacing(extraParagraphSpacing), hyphenationEnabled(hyphenationEnabled) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontStyle fontStyle);
  void setStyle(TextBlock::BLOCK_STYLE style) { this->style = style; }
  TextBlock::BLOCK_STYLE getStyle() const { return style; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, int horizontalMargin,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);

 private:
  std::list<std::string> words;
  std::list<EpdFontStyle> wordStyles;
  TextBlock::BLOCK_STYLE style;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;

  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                        std::vector<uint16_t>& wordWidths, PrefixWidthCache& cache);
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId, int pageWidth,
                                            PrefixWidthCache& cache);
};
