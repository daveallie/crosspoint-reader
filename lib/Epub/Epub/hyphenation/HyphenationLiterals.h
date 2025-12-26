#pragma once

#include <cstddef>
#include <vector>

template <typename T>
struct HyphenLiteral {
  const T* data;
  size_t length;
};

template <typename T>
using HyphenLiteralT = HyphenLiteral<T>;

template <typename WordContainer, typename Literal>
bool matchesLiteralAt(const WordContainer& word, const size_t start, const Literal& literal) {
  if (!literal.data || literal.length == 0) {
    return false;
  }
  if (start + literal.length > word.size()) {
    return false;
  }
  for (size_t i = 0; i < literal.length; ++i) {
    if (word[start + i] != literal.data[i]) {
      return false;
    }
  }
  return true;
}

template <typename WordContainer, typename PrefixContainer, typename SuffixContainer, typename BreakAllowedFn>
void appendLiteralBreaks(const WordContainer& lowerWord, const PrefixContainer& prefixes,
                         const SuffixContainer& suffixes, BreakAllowedFn&& breakAllowed, std::vector<size_t>& indexes) {
  const size_t length = lowerWord.size();

  const auto tryPush = [&](const size_t breakIndex) {
    if (!breakAllowed(breakIndex)) {
      return;
    }
    indexes.push_back(breakIndex);
  };

  for (const auto& literal : prefixes) {
    if (literal.length == 0 || literal.length >= length) {
      continue;
    }
    if (!matchesLiteralAt(lowerWord, 0, literal)) {
      continue;
    }
    tryPush(literal.length);
  }

  for (const auto& literal : suffixes) {
    if (literal.length == 0 || literal.length >= length) {
      continue;
    }
    const size_t breakIndex = length - literal.length;
    if (!matchesLiteralAt(lowerWord, breakIndex, literal)) {
      continue;
    }
    tryPush(breakIndex);
  }
}
