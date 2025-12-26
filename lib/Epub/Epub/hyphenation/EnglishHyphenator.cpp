#include "EnglishHyphenator.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include "HyphenationLiterals.h"

namespace {

char lowerLatinChar(const uint32_t cp) {
  if (!isLatinLetter(cp)) {
    return 0;
  }
  return static_cast<char>(toLowerLatin(cp));
}

bool isEnglishApproximantChar(const char c) { return c == 'l' || c == 'r' || c == 'w' || c == 'y'; }

bool isEnglishStopChar(const char c) {
  switch (c) {
    case 'p':
    case 'b':
    case 't':
    case 'd':
    case 'k':
    case 'g':
    case 'c':
    case 'q':
      return true;
    default:
      return false;
  }
}

bool isEnglishFricativeChar(const char c) {
  switch (c) {
    case 'f':
    case 'v':
    case 's':
    case 'z':
    case 'h':
    case 'x':
      return true;
    default:
      return false;
  }
}

using LatinLiteral = HyphenLiteralT<char>;

constexpr std::array<LatinLiteral, 20> ENGLISH_PREFIXES = {
    {{"anti", 4},  {"auto", 4}, {"counter", 7}, {"de", 2},    {"dis", 3},   {"hyper", 5}, {"inter", 5},
     {"micro", 5}, {"mis", 3},  {"mono", 4},    {"multi", 5}, {"non", 3},   {"over", 4},  {"post", 4},
     {"pre", 3},   {"pro", 3},  {"re", 2},      {"sub", 3},   {"super", 5}, {"trans", 5}}};

constexpr std::array<LatinLiteral, 24> ENGLISH_SUFFIXES = {
    {{"able", 4}, {"ible", 4}, {"ing", 3},  {"ings", 4},   {"ed", 2},    {"er", 2},   {"ers", 3},   {"est", 3},
     {"ful", 3},  {"hood", 4}, {"less", 4}, {"lessly", 6}, {"ly", 2},    {"ment", 4}, {"ments", 5}, {"ness", 4},
     {"ous", 3},  {"tion", 4}, {"sion", 4}, {"ward", 4},   {"wards", 5}, {"ship", 4}, {"ships", 5}, {"y", 1}}};

bool nextToApostrophe(const std::vector<CodepointInfo>& cps, size_t index);

std::string lowercaseLatinWord(const std::vector<CodepointInfo>& cps) {
  std::string lower;
  lower.reserve(cps.size());
  for (const auto& info : cps) {
    lower.push_back(lowerLatinChar(info.value));
  }
  return lower;
}

bool englishSegmentHasVowel(const std::vector<CodepointInfo>& cps, const size_t start, const size_t end) {
  if (start >= end || start >= cps.size()) {
    return false;
  }
  const size_t clampedEnd = std::min(end, cps.size());
  for (size_t i = start; i < clampedEnd; ++i) {
    if (isLatinVowel(cps[i].value)) {
      return true;
    }
  }
  return false;
}

bool englishBreakAllowed(const std::vector<CodepointInfo>& cps, const size_t breakIndex) {
  if (breakIndex == 0 || breakIndex >= cps.size()) {
    return false;
  }

  const size_t prefixLen = breakIndex;
  const size_t suffixLen = cps.size() - breakIndex;
  if (prefixLen < MIN_PREFIX_CP || suffixLen < MIN_SUFFIX_CP) {
    return false;
  }

  if (!englishSegmentHasVowel(cps, 0, breakIndex) || !englishSegmentHasVowel(cps, breakIndex, cps.size())) {
    return false;
  }

  if (nextToApostrophe(cps, breakIndex)) {
    return false;
  }

  return true;
}

void appendMorphologyBreaks(const std::vector<CodepointInfo>& cps, const std::string& lowerWord,
                            std::vector<size_t>& indexes) {
  appendLiteralBreaks(
      lowerWord, ENGLISH_PREFIXES, ENGLISH_SUFFIXES,
      [&](const size_t breakIndex) { return englishBreakAllowed(cps, breakIndex); }, indexes);
}

struct CharPair {
  char first;
  char second;
};

bool matchesDigraph(const char first, const char second, const std::initializer_list<CharPair>& pairs) {
  for (const auto& pair : pairs) {
    if (pair.first == first && pair.second == second) {
      return true;
    }
  }
  return false;
}

bool isEnglishDiphthong(const uint32_t first, const uint32_t second) {
  if (!isLatinLetter(first) || !isLatinLetter(second)) {
    return false;
  }
  const auto f = static_cast<char>(toLowerLatin(first));
  const auto s = static_cast<char>(toLowerLatin(second));
  switch (f) {
    case 'a':
      return s == 'i' || s == 'y' || s == 'u';
    case 'e':
      return s == 'a' || s == 'e' || s == 'i' || s == 'o' || s == 'u' || s == 'y';
    case 'i':
      return s == 'e' || s == 'u' || s == 'a';
    case 'o':
      return s == 'a' || s == 'e' || s == 'i' || s == 'o' || s == 'u' || s == 'y';
    case 'u':
      return s == 'i' || s == 'a' || s == 'e';
  }
  return false;
}

bool isValidEnglishOnsetBigram(const uint32_t firstCp, const uint32_t secondCp) {
  const char first = lowerLatinChar(firstCp);
  const char second = lowerLatinChar(secondCp);
  if (!first || !second) {
    return false;
  }

  if (matchesDigraph(first, second,
                     {{'c', 'h'},
                      {'s', 'h'},
                      {'t', 'h'},
                      {'p', 'h'},
                      {'w', 'h'},
                      {'w', 'r'},
                      {'k', 'n'},
                      {'g', 'n'},
                      {'p', 's'},
                      {'p', 't'},
                      {'p', 'n'},
                      {'r', 'h'}})) {
    return true;
  }

  if (isEnglishStopChar(first) && isEnglishApproximantChar(second)) {
    return true;
  }

  if (isEnglishFricativeChar(first) && isEnglishApproximantChar(second)) {
    return true;
  }

  if (first == 's' && (second == 'p' || second == 't' || second == 'k' || second == 'm' || second == 'n' ||
                       second == 'f' || second == 'l' || second == 'w' || second == 'c')) {
    return true;
  }

  if (second == 'y' && (first == 'p' || first == 'b' || first == 't' || first == 'd' || first == 'f' || first == 'k' ||
                        first == 'g' || first == 'h' || first == 'm' || first == 'n' || first == 'l' || first == 's')) {
    return true;
  }

  return false;
}

bool isValidEnglishOnsetTrigram(const uint32_t firstCp, const uint32_t secondCp, const uint32_t thirdCp) {
  const char first = lowerLatinChar(firstCp);
  const char second = lowerLatinChar(secondCp);
  const char third = lowerLatinChar(thirdCp);
  if (!first || !second || !third) {
    return false;
  }

  if (first == 's') {
    if (second == 'p' && (third == 'l' || third == 'r' || third == 'w')) {
      return true;
    }
    if (second == 't' && (third == 'r' || third == 'w' || third == 'y')) {
      return true;
    }
    if (second == 'k' && (third == 'l' || third == 'r' || third == 'w')) {
      return true;
    }
    if (second == 'c' && (third == 'l' || third == 'r')) {
      return true;
    }
    if (second == 'f' && third == 'r') {
      return true;
    }
    if (second == 'h' && third == 'r') {
      return true;
    }
  }

  if (first == 't' && second == 'h' && third == 'r') {
    return true;
  }

  return false;
}

// Verifies that the consonant cluster could begin an English syllable.
bool englishClusterIsValidOnset(const std::vector<CodepointInfo>& cps, const size_t start, const size_t end) {
  if (start >= end) {
    return false;
  }

  for (size_t i = start; i < end; ++i) {
    const char ch = lowerLatinChar(cps[i].value);
    if (!ch) {
      return false;
    }
    if (!isLatinConsonant(cps[i].value) && ch != 'y') {
      return false;
    }
  }

  const size_t len = end - start;
  if (len == 1) {
    return true;
  }
  if (len == 2) {
    return isValidEnglishOnsetBigram(cps[start].value, cps[start + 1].value);
  }
  if (len == 3) {
    return isValidEnglishOnsetTrigram(cps[start].value, cps[start + 1].value, cps[start + 2].value);
  }

  return false;
}

// Picks the longest legal onset inside the consonant cluster between vowels.
size_t englishOnsetLength(const std::vector<CodepointInfo>& cps, const size_t clusterStart, const size_t clusterEnd) {
  const size_t clusterLen = clusterEnd - clusterStart;
  if (clusterLen == 0) {
    return 0;
  }

  const size_t maxLen = std::min<size_t>(3, clusterLen);
  for (size_t len = maxLen; len >= 1; --len) {
    const size_t suffixStart = clusterEnd - len;
    if (englishClusterIsValidOnset(cps, suffixStart, clusterEnd)) {
      return len;
    }
  }

  return 1;
}

// Avoids creating hyphen positions adjacent to apostrophes (e.g., contractions).
bool nextToApostrophe(const std::vector<CodepointInfo>& cps, const size_t index) {
  if (index == 0 || index >= cps.size()) {
    return false;
  }
  const auto left = cps[index - 1].value;
  const auto right = cps[index].value;
  return left == '\'' || right == '\'';
}

// Returns byte indexes where the word may break according to English syllable rules.
std::vector<size_t> englishBreakIndexes(const std::vector<CodepointInfo>& cps) {
  std::vector<size_t> indexes;
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return indexes;
  }

  const auto lowerWord = lowercaseLatinWord(cps);
  std::vector<size_t> vowelPositions;
  vowelPositions.reserve(cps.size());
  for (size_t i = 0; i < cps.size(); ++i) {
    if (isLatinVowel(cps[i].value)) {
      vowelPositions.push_back(i);
    }
  }

  if (vowelPositions.size() < 2) {
    return indexes;
  }

  for (size_t v = 0; v + 1 < vowelPositions.size(); ++v) {
    const size_t leftVowel = vowelPositions[v];
    const size_t rightVowel = vowelPositions[v + 1];

    if (rightVowel - leftVowel == 1) {
      if (!isEnglishDiphthong(cps[leftVowel].value, cps[rightVowel].value) && englishBreakAllowed(cps, rightVowel)) {
        indexes.push_back(rightVowel);
      }
      continue;
    }

    const size_t clusterStart = leftVowel + 1;
    const size_t clusterEnd = rightVowel;
    const size_t onsetLen = englishOnsetLength(cps, clusterStart, clusterEnd);
    size_t breakIndex = clusterEnd - onsetLen;

    if (!englishBreakAllowed(cps, breakIndex)) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  appendMorphologyBreaks(cps, lowerWord, indexes);

  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  return indexes;
}

}  // namespace

const EnglishHyphenator& EnglishHyphenator::instance() {
  static EnglishHyphenator instance;
  return instance;
}

Script EnglishHyphenator::script() const { return Script::Latin; }

std::vector<size_t> EnglishHyphenator::breakIndexes(const std::vector<CodepointInfo>& cps) const {
  return englishBreakIndexes(cps);
}
