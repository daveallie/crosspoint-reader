#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Serialization.h>

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  auto wordIt = words.begin();
  auto wordStylesIt = wordStyles.begin();
  auto wordXposIt = wordXpos.begin();

  for (int i = 0; i < words.size(); i++) {
    renderer.drawText(fontId, *wordXposIt + x, y, wordIt->c_str(), true, *wordStylesIt);

    std::advance(wordIt, 1);
    std::advance(wordStylesIt, 1);
    std::advance(wordXposIt, 1);
  }
}

void TextBlock::serialize(File& file) const {
  // words
  const uint32_t wc = words.size();
  serialization::writePod(file, wc);
  for (const auto& w : words) serialization::writeString(file, w);

  // wordXpos
  const uint32_t xc = wordXpos.size();
  serialization::writePod(file, xc);
  for (auto x : wordXpos) serialization::writePod(file, x);

  // wordStyles
  const uint32_t sc = wordStyles.size();
  serialization::writePod(file, sc);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // style
  serialization::writePod(file, style);
}

std::unique_ptr<TextBlock> TextBlock::deserialize(File& file) {
  uint32_t wc, xc, sc;
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontStyle> wordStyles;
  BLOCK_STYLE style;

  // words
  serialization::readPod(file, wc);
  words.resize(wc);
  for (auto& w : words) serialization::readString(file, w);

  // wordXpos
  serialization::readPod(file, xc);
  wordXpos.resize(xc);
  for (auto& x : wordXpos) serialization::readPod(file, x);

  // wordStyles
  serialization::readPod(file, sc);
  wordStyles.resize(sc);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // style
  serialization::readPod(file, style);

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), style));
}
