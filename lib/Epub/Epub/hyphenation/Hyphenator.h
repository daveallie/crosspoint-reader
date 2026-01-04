#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Hyphenator {
 public:
  // Returns byte offsets where the word may be hyphenated. When includeFallback is true, all positions obeying the
  // minimum prefix/suffix constraints are returned even if no language-specific rule matches.
  static std::vector<size_t> breakOffsets(const std::string& word, bool includeFallback);
};