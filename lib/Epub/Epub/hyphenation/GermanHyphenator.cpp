#include "GermanHyphenator.h"

#include "LiangHyphenation.h"
#include "generated/hyph-de.trie.h"

const GermanHyphenator& GermanHyphenator::instance() {
  static GermanHyphenator instance;
  return instance;
}

std::vector<size_t> GermanHyphenator::breakIndexes(const std::vector<CodepointInfo>& cps) const {
  const LiangWordConfig config(isLatinLetter, toLowerLatin, minPrefix(), minSuffix());
  return liangBreakIndexes(cps, de_patterns, config);
}
