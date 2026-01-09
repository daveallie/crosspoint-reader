#pragma once

#include "LanguageHyphenator.h"

// Implements syllable-aware break calculation for Latin-script (English) words.
class EnglishHyphenator final : public LanguageHyphenator {
 public:
  static const EnglishHyphenator& instance();

  std::vector<size_t> breakIndexes(const std::vector<CodepointInfo>& cps) const override;
  // Keep both minima at two characters to mirror Pyphen defaults.
  size_t minPrefix() const override { return 2; }
  size_t minSuffix() const override { return 2; }

 private:
  EnglishHyphenator() = default;
};
