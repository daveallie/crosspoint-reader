#pragma once

#include "LanguageHyphenator.h"

// Implements Liang hyphenation rules for German (Latin script).
class GermanHyphenator final : public LanguageHyphenator {
 public:
  static const GermanHyphenator& instance();

  std::vector<size_t> breakIndexes(const std::vector<CodepointInfo>& cps) const override;

 private:
  GermanHyphenator() = default;
};
