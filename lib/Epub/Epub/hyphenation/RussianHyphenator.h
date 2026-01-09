#pragma once

#include "LanguageHyphenator.h"

// Handles Cyrillic-specific hyphenation heuristics (Russian syllable rules).
class RussianHyphenator final : public LanguageHyphenator {
 public:
  static const RussianHyphenator& instance();

  std::vector<size_t> breakIndexes(const std::vector<CodepointInfo>& cps) const override;
  size_t minPrefix() const override { return 2; }
  size_t minSuffix() const override { return 2; }

 private:
  RussianHyphenator() = default;
};
