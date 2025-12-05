#include "EpdRenderer.h"

#include "builtinFonts/babyblue.h"
#include "builtinFonts/bookerly.h"
#include "builtinFonts/bookerly_bold.h"
#include "builtinFonts/bookerly_bold_italic.h"
#include "builtinFonts/bookerly_italic.h"
#include "builtinFonts/ubuntu_10.h"
#include "builtinFonts/ubuntu_bold_10.h"

EpdRenderer::EpdRenderer(XteinkDisplay* display) {
  const auto bookerlyFontFamily = new EpdFontFamily(new EpdFont(&bookerly), new EpdFont(&bookerly_bold),
                                                    new EpdFont(&bookerly_italic), new EpdFont(&bookerly_bold_italic));
  const auto ubuntuFontFamily = new EpdFontFamily(new EpdFont(&ubuntu_10), new EpdFont(&ubuntu_bold_10));

  this->display = display;
  this->regularFontRenderer = new EpdFontRenderer<XteinkDisplay>(bookerlyFontFamily, display);
  this->smallFontRenderer = new EpdFontRenderer<XteinkDisplay>(new EpdFontFamily(new EpdFont(&babyblue)), display);
  this->uiFontRenderer = new EpdFontRenderer<XteinkDisplay>(ubuntuFontFamily, display);

  this->marginTop = 11;
  this->marginBottom = 30;
  this->marginLeft = 10;
  this->marginRight = 10;
  this->lineCompression = 0.95f;
}

int EpdRenderer::getTextWidth(const char* text, const EpdFontStyle style) const {
  int w = 0, h = 0;

  regularFontRenderer->fontFamily->getTextDimensions(text, &w, &h, style);

  return w;
}

int EpdRenderer::getUiTextWidth(const char* text, const EpdFontStyle style) const {
  int w = 0, h = 0;

  uiFontRenderer->fontFamily->getTextDimensions(text, &w, &h, style);

  return w;
}

int EpdRenderer::getSmallTextWidth(const char* text, const EpdFontStyle style) const {
  int w = 0, h = 0;

  smallFontRenderer->fontFamily->getTextDimensions(text, &w, &h, style);

  return w;
}

void EpdRenderer::drawText(const int x, const int y, const char* text, const uint16_t color,
                           const EpdFontStyle style) const {
  int ypos = y + getLineHeight() + marginTop;
  int xpos = x + marginLeft;
  regularFontRenderer->renderString(text, &xpos, &ypos, color > 0 ? GxEPD_BLACK : GxEPD_WHITE, style);
}

void EpdRenderer::drawUiText(const int x, const int y, const char* text, const uint16_t color,
                             const EpdFontStyle style) const {
  int ypos = y + uiFontRenderer->fontFamily->getData(style)->advanceY + marginTop;
  int xpos = x + marginLeft;
  uiFontRenderer->renderString(text, &xpos, &ypos, color > 0 ? GxEPD_BLACK : GxEPD_WHITE, style);
}

void EpdRenderer::drawSmallText(const int x, const int y, const char* text, const uint16_t color,
                                const EpdFontStyle style) const {
  int ypos = y + smallFontRenderer->fontFamily->getData(style)->advanceY + marginTop;
  int xpos = x + marginLeft;
  smallFontRenderer->renderString(text, &xpos, &ypos, color > 0 ? GxEPD_BLACK : GxEPD_WHITE, style);
}

void EpdRenderer::drawTextBox(const int x, const int y, const std::string& text, const int width, const int height,
                              const EpdFontStyle style) const {
  const size_t length = text.length();
  // fit the text into the box
  int start = 0;
  int end = 1;
  int ypos = 0;
  while (true) {
    if (end >= length) {
      drawText(x, y + ypos, text.substr(start, length - start).c_str(), 1, style);
      break;
    }

    if (ypos + getLineHeight() >= height) {
      break;
    }

    if (text[end - 1] == '\n') {
      drawText(x, y + ypos, text.substr(start, end - start).c_str(), 1, style);
      ypos += getLineHeight();
      start = end;
      end = start + 1;
      continue;
    }

    if (getTextWidth(text.substr(start, end - start).c_str(), style) > width) {
      drawText(x, y + ypos, text.substr(start, end - start - 1).c_str(), 1, style);
      ypos += getLineHeight();
      start = end - 1;
      continue;
    }

    end++;
  }
}

void EpdRenderer::drawLine(int x1, int y1, int x2, int y2, uint16_t color) const {
  display->drawLine(x1 + marginLeft, y1 + marginTop, x2 + marginLeft, y2 + marginTop,
                    color > 0 ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::drawRect(const int x, const int y, const int width, const int height, const uint16_t color) const {
  display->drawRect(x + marginLeft, y + marginTop, width, height, color > 0 ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::fillRect(const int x, const int y, const int width, const int height, const uint16_t color) const {
  display->fillRect(x + marginLeft, y + marginTop, width, height, color > 0 ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::drawCircle(const int x, const int y, const int radius, const uint16_t color) const {
  display->drawCircle(x + marginLeft, y + marginTop, radius, color > 0 ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::fillCircle(const int x, const int y, const int radius, const uint16_t color) const {
  display->fillCircle(x + marginLeft, y + marginTop, radius, color > 0 ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height,
                            const bool invert, const bool mirrorY) const {
  drawImageNoMargin(bitmap, x + marginLeft, y + marginTop, width, height, invert, mirrorY);
}

void EpdRenderer::drawImageNoMargin(const uint8_t bitmap[], const int x, const int y, const int width, const int height,
                                    const bool invert, const bool mirrorY) const {
  display->drawImage(bitmap, x, y, width, height, invert, mirrorY);
}

void EpdRenderer::clearScreen(const bool black) const {
  Serial.println("Clearing screen");
  display->fillScreen(black ? GxEPD_BLACK : GxEPD_WHITE);
}

void EpdRenderer::flushDisplay(const bool partialUpdate) const { display->display(partialUpdate); }

void EpdRenderer::flushArea(const int x, const int y, const int width, const int height) const {
  display->displayWindow(x + marginLeft, y + marginTop, width, height);
}

int EpdRenderer::getPageWidth() const { return display->width() - marginLeft - marginRight; }

int EpdRenderer::getPageHeight() const { return display->height() - marginTop - marginBottom; }

int EpdRenderer::getSpaceWidth() const { return regularFontRenderer->fontFamily->getGlyph(' ', REGULAR)->advanceX; }

int EpdRenderer::getLineHeight() const {
  return regularFontRenderer->fontFamily->getData(REGULAR)->advanceY * lineCompression;
}
