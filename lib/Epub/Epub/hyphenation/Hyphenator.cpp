#include "Hyphenator.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "EnglishHyphenator.h"
#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "RussianHyphenator.h"

namespace {

// Central registry for language-specific hyphenators supported on device.
const std::array<const LanguageHyphenator*, 2>& registeredHyphenators() {
  static const std::array<const LanguageHyphenator*, 2> hyphenators = {
      &EnglishHyphenator::instance(),
      &RussianHyphenator::instance(),
  };
  return hyphenators;
}

// Finds the hyphenator matching the detected script.
const LanguageHyphenator* hyphenatorForScript(const Script script) {
  for (const auto* hyphenator : registeredHyphenators()) {
    if (hyphenator->script() == script) {
      return hyphenator;
    }
  }
  return nullptr;
}

// Converts the UTF-8 word into codepoint metadata for downstream rules.
std::vector<CodepointInfo> collectCodepoints(const std::string& word) {
  std::vector<CodepointInfo> cps;
  cps.reserve(word.size());

  const unsigned char* base = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* ptr = base;
  while (*ptr != 0) {
    const unsigned char* current = ptr;
    const uint32_t cp = utf8NextCodepoint(&ptr);
    cps.push_back({cp, static_cast<size_t>(current - base)});
  }

  return cps;
}

// Rejects words containing punctuation or digits unless forced.
bool hasOnlyAlphabetic(const std::vector<CodepointInfo>& cps) {
  if (cps.empty()) {
    return false;
  }

  for (const auto& info : cps) {
    if (!isAlphabetic(info.value)) {
      return false;
    }
  }
  return true;
}

// Asks the language hyphenator for legal break positions inside the word.
std::vector<size_t> collectBreakIndexes(const std::vector<CodepointInfo>& cps) {
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return {};
  }

  const Script script = detectScript(cps);
  if (const auto* hyphenator = hyphenatorForScript(script)) {
    auto indexes = hyphenator->breakIndexes(cps);
    return indexes;
  }

  return {};
}

// Maps a codepoint index back to its byte offset inside the source word.
size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  if (index >= cps.size()) {
    return cps.empty() ? 0 : cps.back().byteOffset;
  }
  return cps[index].byteOffset;
}

// Safely slices a UTF-8 string without splitting multibyte sequences.
std::string slice(const std::string& word, const size_t startByte, const size_t endByte) {
  if (startByte >= endByte || startByte >= word.size()) {
    return std::string();
  }
  const size_t boundedEnd = std::min(endByte, word.size());
  return word.substr(startByte, boundedEnd - startByte);
}

}  // namespace

bool Hyphenator::splitWord(const GfxRenderer& renderer, const int fontId, const std::string& word,
                           const EpdFontStyle style, const int availableWidth, HyphenationResult* result,
                           const bool force) {
  if (!result || word.empty()) {
    return false;
  }

  auto cps = collectCodepoints(word);
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return false;
  }

  if (!force && !hasOnlyAlphabetic(cps)) {
    return false;
  }

  const auto breakIndexes = collectBreakIndexes(cps);
  const int hyphenWidth = renderer.getTextWidth(fontId, "-", style);
  const int adjustedWidth = availableWidth - hyphenWidth;

  size_t chosenIndex = std::numeric_limits<size_t>::max();

  if (adjustedWidth > 0) {
    for (const size_t idx : breakIndexes) {
      const size_t byteOffset = byteOffsetForIndex(cps, idx);
      const std::string prefix = word.substr(0, byteOffset);
      const int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), style);
      if (prefixWidth <= adjustedWidth) {
        chosenIndex = idx;
      } else {
        break;
      }
    }
  }

  if (chosenIndex == std::numeric_limits<size_t>::max() && force) {
    // Emergency fallback: brute-force through codepoints to avoid overflow when no legal breaks fit.
    for (size_t idx = MIN_PREFIX_CP; idx + MIN_SUFFIX_CP <= cps.size(); ++idx) {
      const size_t byteOffset = byteOffsetForIndex(cps, idx);
      const std::string prefix = word.substr(0, byteOffset);
      const int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), style);
      if (adjustedWidth <= 0 || prefixWidth <= adjustedWidth) {
        chosenIndex = idx;
        if (adjustedWidth > 0 && prefixWidth > adjustedWidth) {
          break;
        }
      }
    }
  }

  if (chosenIndex == std::numeric_limits<size_t>::max()) {
    return false;
  }

  const size_t splitByte = byteOffsetForIndex(cps, chosenIndex);
  const std::string head = word.substr(0, splitByte);
  const std::string tail = slice(word, splitByte, word.size());

  if (head.empty() || tail.empty()) {
    return false;
  }

  result->head = head + "-";
  result->tail = tail;
  return true;
}
