#pragma once

#include <cstddef>
#include <vector>

#include "HyphenationCommon.h"

class LanguageHyphenator {
 public:
  static constexpr size_t kDefaultMinPrefix = 2;
  static constexpr size_t kDefaultMinSuffix = 2;

  virtual ~LanguageHyphenator() = default;
  virtual std::vector<size_t> breakIndexes(const std::vector<CodepointInfo>& cps) const = 0;
  virtual size_t minPrefix() const { return kDefaultMinPrefix; }
  virtual size_t minSuffix() const { return kDefaultMinSuffix; }
};
