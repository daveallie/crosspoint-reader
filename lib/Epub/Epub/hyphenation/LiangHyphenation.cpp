#include "LiangHyphenation.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace {

// Holds the dotted, lower-case representation used by Liang along with the original character order
// so we can traverse via Unicode scalars instead of raw UTF-8 bytes.
struct AugmentedWord {
  std::vector<uint32_t> chars;

  bool empty() const { return chars.empty(); }
  size_t charCount() const { return chars.size(); }
};

// Adds a single character to the augmented word.
void appendCharToAugmentedWord(uint32_t cp, AugmentedWord& word) { word.chars.push_back(cp); }

// Produces the dotted ('.' + lowercase word + '.') UTF-8 byte stream required by Liang.  Classic TeX
// hyphenation logic prepends/appends '.' sentinels so that patterns like ".ab" may anchor to word
// boundaries.  If any character in the candidate word fails the `isLetter` predicate we abort early
// and return an empty structure, signaling the caller to skip hyphenation entirely.
AugmentedWord buildAugmentedWord(const std::vector<CodepointInfo>& cps, const LiangWordConfig& config) {
  AugmentedWord word;
  if (cps.empty()) {
    return word;
  }

  word.chars.reserve(cps.size() + 2);

  appendCharToAugmentedWord('.', word);
  for (const auto& info : cps) {
    if (!config.isLetter(info.value)) {
      word.chars.clear();
      return word;
    }
    appendCharToAugmentedWord(config.toLower(info.value), word);
  }
  appendCharToAugmentedWord('.', word);
  return word;
}

// Compact header that prefixes every serialized trie blob and lets us locate
// the individual sections without storing pointers in flash.
struct SerializedTrieHeader {
  uint32_t letterCount;
  uint32_t nodeCount;
  uint32_t edgeCount;
  uint32_t valueBytes;
};

constexpr size_t kNodeRecordSize = 7;
constexpr uint32_t kNoValueOffset = 0x00FFFFFFu;

// Lightweight view over the packed blob emitted by the generator script.
struct SerializedTrieView {
  const uint32_t* letters = nullptr;
  const uint8_t* nodes = nullptr;
  const uint8_t* edgeChildren = nullptr;
  const uint8_t* edgeLetters = nullptr;
  const uint8_t* values = nullptr;
  uint32_t letterCount = 0;
  uint32_t nodeCount = 0;
  uint32_t edgeCount = 0;
  uint32_t valueBytes = 0;
  size_t edgeLetterBytes = 0;

  static constexpr size_t kInvalidNodeIndex = std::numeric_limits<size_t>::max();
  static constexpr uint32_t kInvalidLetterIndex = std::numeric_limits<uint32_t>::max();
};

// Splits the raw byte array into typed slices. We purposely keep this logic
// very defensive: any malformed blob results in an empty view so the caller can
// bail out quietly.
SerializedTrieView parseSerializedTrie(const SerializedHyphenationPatterns& patterns) {
  SerializedTrieView view;
  if (!patterns.data || patterns.size < sizeof(SerializedTrieHeader)) {
    return view;
  }

  const auto* header = reinterpret_cast<const SerializedTrieHeader*>(patterns.data);
  const uint8_t* cursor = patterns.data + sizeof(SerializedTrieHeader);
  const uint8_t* end = patterns.data + patterns.size;

  const auto requireBytes = [&](size_t bytes) { return bytes <= static_cast<size_t>(end - cursor); };

  const size_t lettersBytes = static_cast<size_t>(header->letterCount) * sizeof(uint32_t);
  if (!requireBytes(lettersBytes)) {
    return SerializedTrieView{};
  }
  view.letters = reinterpret_cast<const uint32_t*>(cursor);
  cursor += lettersBytes;

  const size_t nodesBytes = static_cast<size_t>(header->nodeCount) * kNodeRecordSize;
  if (!requireBytes(nodesBytes)) {
    return SerializedTrieView{};
  }
  view.nodes = cursor;
  cursor += nodesBytes;

  const size_t childBytes = static_cast<size_t>(header->edgeCount) * sizeof(uint16_t);
  if (!requireBytes(childBytes)) {
    return SerializedTrieView{};
  }
  view.edgeChildren = cursor;
  cursor += childBytes;

  const size_t letterBits = static_cast<size_t>(header->edgeCount) * 6;
  const size_t letterBytes = (letterBits + 7) >> 3;
  if (!requireBytes(letterBytes)) {
    return SerializedTrieView{};
  }
  view.edgeLetters = cursor;
  view.edgeLetterBytes = letterBytes;
  cursor += letterBytes;

  if (!requireBytes(header->valueBytes)) {
    return SerializedTrieView{};
  }
  view.values = cursor;
  view.valueBytes = header->valueBytes;

  view.letterCount = header->letterCount;
  view.nodeCount = header->nodeCount;
  view.edgeCount = header->edgeCount;
  return view;
}

// The serialized blobs live in PROGMEM, so parsing them repeatedly is cheap but
// wasteful. Keep a tiny cache indexed by the descriptor address so every
// language builds its view only once.
const SerializedTrieView& getSerializedTrie(const SerializedHyphenationPatterns& patterns) {
  struct CacheEntry {
    const SerializedHyphenationPatterns* key;
    SerializedTrieView view;
  };
  static std::vector<CacheEntry> cache;

  for (const auto& entry : cache) {
    if (entry.key == &patterns) {
      return entry.view;
    }
  }

  cache.push_back({&patterns, parseSerializedTrie(patterns)});
  return cache.back().view;
}

uint16_t readUint16LE(const uint8_t* ptr) {
  return static_cast<uint16_t>(ptr[0]) | static_cast<uint16_t>(static_cast<uint16_t>(ptr[1]) << 8);
}

uint32_t readUint24LE(const uint8_t* ptr) {
  return static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8) | (static_cast<uint32_t>(ptr[2]) << 16);
}

// Edges store child indexes and letter indexes in separate, compact arrays. We
// read the child from the 16-bit table and decode the 6-bit letter from the
// bitstream, which packs two entries per 12 bits on average.
uint8_t readEdgeLetterIndex(const SerializedTrieView& trie, const size_t edgeIndex) {
  if (!trie.edgeLetters) {
    return 0xFFu;
  }
  const size_t bitOffset = edgeIndex * 6;
  const size_t byteOffset = bitOffset >> 3;
  if (byteOffset >= trie.edgeLetterBytes) {
    return 0xFFu;
  }
  const uint8_t bitShift = static_cast<uint8_t>(bitOffset & 0x07u);
  uint32_t chunk = trie.edgeLetters[byteOffset];
  if (byteOffset + 1 < trie.edgeLetterBytes) {
    chunk |= static_cast<uint32_t>(trie.edgeLetters[byteOffset + 1]) << 8;
  }
  const uint8_t value = static_cast<uint8_t>((chunk >> bitShift) & 0x3Fu);
  return value;
}

// Materialized view of a node record so call sites do not repeatedly poke into
// the byte array.
struct NodeFields {
  uint16_t firstEdge;
  uint8_t childCount;
  uint32_t valueOffset;
  uint8_t valueLength;
};

NodeFields loadNode(const SerializedTrieView& trie, const size_t nodeIndex) {
  NodeFields fields{0, 0, kNoValueOffset, 0};
  if (!trie.nodes || nodeIndex >= trie.nodeCount) {
    return fields;
  }

  const uint8_t* entry = trie.nodes + nodeIndex * kNodeRecordSize;
  fields.firstEdge = readUint16LE(entry);
  fields.childCount = entry[2];
  fields.valueOffset = readUint24LE(entry + 3);
  fields.valueLength = entry[6];
  return fields;
}

// Letter indexes are stored sorted, so a lower_bound gives us O(log n) lookups
// without building auxiliary maps.
uint32_t letterIndexForCodepoint(const SerializedTrieView& trie, const uint32_t cp) {
  if (!trie.letters || trie.letterCount == 0) {
    return SerializedTrieView::kInvalidLetterIndex;
  }
  const uint32_t* begin = trie.letters;
  const uint32_t* end = begin + trie.letterCount;
  const auto it = std::lower_bound(begin, end, cp);
  if (it == end || *it != cp) {
    return SerializedTrieView::kInvalidLetterIndex;
  }
  return static_cast<uint32_t>(it - begin);
}

// Walks the child edge slice described by the node record using binary search
// on the inlined letter indexes. Returns kInvalidNodeIndex when the path ends.
size_t findChild(const SerializedTrieView& trie, const size_t nodeIndex, const uint32_t letter) {
  const uint32_t letterIndex = letterIndexForCodepoint(trie, letter);
  if (letterIndex == SerializedTrieView::kInvalidLetterIndex) {
    return SerializedTrieView::kInvalidNodeIndex;
  }
  if (!trie.edgeChildren || !trie.edgeLetters) {
    return SerializedTrieView::kInvalidNodeIndex;
  }

  const NodeFields node = loadNode(trie, nodeIndex);
  size_t low = 0;
  size_t high = node.childCount;
  while (low < high) {
    const size_t mid = low + ((high - low) >> 1);
    const size_t edgeIndex = static_cast<size_t>(node.firstEdge) + mid;
    if (edgeIndex >= trie.edgeCount) {
      return SerializedTrieView::kInvalidNodeIndex;
    }
    const uint32_t entryLetterIndex = readEdgeLetterIndex(trie, edgeIndex);
    if (entryLetterIndex == letterIndex) {
      const uint8_t* childPtr = trie.edgeChildren + edgeIndex * sizeof(uint16_t);
      return readUint16LE(childPtr);
    }
    if (entryLetterIndex < letterIndex) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return SerializedTrieView::kInvalidNodeIndex;
}

// Merges the pattern's numeric priorities into the global score array (max per slot).
void applyPatternValues(const SerializedTrieView& trie, const NodeFields& node, const size_t startCharIndex,
                        std::vector<uint8_t>& scores) {
  if (node.valueLength == 0 || node.valueOffset == kNoValueOffset || !trie.values ||
      node.valueOffset >= trie.valueBytes) {
    return;
  }

  const size_t availableBytes = trie.valueBytes - node.valueOffset;
  const size_t packedBytesNeeded = (static_cast<size_t>(node.valueLength) + 1) >> 1;
  const size_t packedBytes = std::min<size_t>(packedBytesNeeded, availableBytes);
  const uint8_t* packedValues = trie.values + node.valueOffset;
  // Value digits remain nibble-encoded (two per byte) to keep flash usage low;
  // expand back to single scores just before applying them.
  for (size_t valueIdx = 0; valueIdx < node.valueLength; ++valueIdx) {
    const size_t packedIndex = valueIdx >> 1;
    if (packedIndex >= packedBytes) {
      break;
    }
    const uint8_t packedByte = packedValues[packedIndex];
    const uint8_t value =
        (valueIdx & 1u) ? static_cast<uint8_t>((packedByte >> 4) & 0x0Fu) : static_cast<uint8_t>(packedByte & 0x0Fu);
    const size_t scoreIdx = startCharIndex + valueIdx;
    if (scoreIdx >= scores.size()) {
      break;
    }
    scores[scoreIdx] = std::max(scores[scoreIdx], value);
  }
}

// Converts odd score positions back into codepoint indexes, honoring min prefix/suffix constraints.
// By iterating over codepoint indexes rather than raw byte offsets we naturally support UTF-8 input
// without bookkeeping gymnastics.  Each break corresponds to scores[breakIndex + 1] because of the
// leading '.' sentinel emitted in buildAugmentedWord().
std::vector<size_t> collectBreakIndexes(const std::vector<CodepointInfo>& cps, const std::vector<uint8_t>& scores,
                                        const size_t minPrefix, const size_t minSuffix) {
  std::vector<size_t> indexes;
  const size_t cpCount = cps.size();
  if (cpCount < 2) {
    return indexes;
  }

  for (size_t breakIndex = 1; breakIndex < cpCount; ++breakIndex) {
    if (breakIndex < minPrefix) {
      continue;
    }

    const size_t suffixCount = cpCount - breakIndex;
    if (suffixCount < minSuffix) {
      continue;
    }

    const size_t scoreIdx = breakIndex + 1;  // Account for leading '.' sentinel.
    if (scoreIdx >= scores.size()) {
      break;
    }
    if ((scores[scoreIdx] & 1u) == 0) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  return indexes;
}

}  // namespace

std::vector<size_t> liangBreakIndexes(const std::vector<CodepointInfo>& cps,
                                      const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config) {
  // Step 1: convert the input word into the dotted UTF-8 stream the Liang algorithm expects.  A return
  // value of {} means the word contained something outside the language's alphabet and should be left
  // untouched by hyphenation.
  const auto augmented = buildAugmentedWord(cps, config);
  if (augmented.empty()) {
    return {};
  }

  // Step 2: run the augmented word through the trie-backed pattern table so we reuse common prefixes
  // instead of rescanning the UTF-8 bytes for every substring.
  const SerializedTrieView& trie = getSerializedTrie(patterns);
  if (!trie.nodes || trie.nodeCount == 0) {
    return {};
  }
  const size_t charCount = augmented.charCount();
  std::vector<uint8_t> scores(charCount + 1, 0);
  for (size_t charStart = 0; charStart < charCount; ++charStart) {
    size_t currentNode = 0;  // Root node.
    for (size_t cursor = charStart; cursor < charCount; ++cursor) {
      const uint32_t letter = augmented.chars[cursor];
      currentNode = findChild(trie, currentNode, letter);
      if (currentNode == SerializedTrieView::kInvalidNodeIndex) {
        break;
      }

      const NodeFields node = loadNode(trie, currentNode);
      if (node.valueLength > 0 && node.valueOffset != kNoValueOffset) {
        applyPatternValues(trie, node, charStart, scores);
      }
    }
  }

  // Step 3: translate odd-numbered score positions back into codepoint indexes, enforcing per-language
  // prefix/suffix minima so we do not produce visually awkward fragments.
  return collectBreakIndexes(cps, scores, config.minPrefix, config.minSuffix);
}
