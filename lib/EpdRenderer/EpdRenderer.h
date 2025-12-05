#pragma once

#include <GxEPD2_BW.h>

#include <EpdFontRenderer.hpp>

#define XteinkDisplay GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT>

class EpdRenderer {
  XteinkDisplay* display;
  EpdFontRenderer<XteinkDisplay>* regularFontRenderer;
  EpdFontRenderer<XteinkDisplay>* smallFontRenderer;
  EpdFontRenderer<XteinkDisplay>* uiFontRenderer;
  int marginTop;
  int marginBottom;
  int marginLeft;
  int marginRight;
  float lineCompression;

 public:
  explicit EpdRenderer(XteinkDisplay* display);
  ~EpdRenderer() = default;
  int getTextWidth(const char* text, EpdFontStyle style = REGULAR) const;
  int getUiTextWidth(const char* text, EpdFontStyle style = REGULAR) const;
  int getSmallTextWidth(const char* text, EpdFontStyle style = REGULAR) const;
  void drawText(int x, int y, const char* text, uint16_t color = 1, EpdFontStyle style = REGULAR) const;
  void drawUiText(int x, int y, const char* text, uint16_t color = 1, EpdFontStyle style = REGULAR) const;
  void drawSmallText(int x, int y, const char* text, uint16_t color = 1, EpdFontStyle style = REGULAR) const;
  void drawTextBox(int x, int y, const std::string& text, int width, int height, EpdFontStyle style = REGULAR) const;
  void drawLine(int x1, int y1, int x2, int y2, uint16_t color = 1) const;
  void drawRect(int x, int y, int width, int height, uint16_t color = 1) const;
  void fillRect(int x, int y, int width, int height, uint16_t color = 1) const;
  void drawCircle(int x, int y, int radius, uint16_t color = 1) const;
  void fillCircle(int x, int y, int radius, uint16_t color = 1) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height, bool invert = false,
                 bool mirrorY = false) const;
  void drawImageNoMargin(const uint8_t bitmap[], int x, int y, int width, int height, bool invert = false,
                         bool mirrorY = false) const;
  void clearScreen(bool black = false) const;
  void flushDisplay(bool partialUpdate = true) const;
  void flushArea(int x, int y, int width, int height) const;

  int getPageWidth() const;
  int getPageHeight() const;
  int getSpaceWidth() const;
  int getLineHeight() const;
  // set margins
  void setMarginTop(const int newMarginTop) { this->marginTop = newMarginTop; }
  void setMarginBottom(const int newMarginBottom) { this->marginBottom = newMarginBottom; }
  void setMarginLeft(const int newMarginLeft) { this->marginLeft = newMarginLeft; }
  void setMarginRight(const int newMarginRight) { this->marginRight = newMarginRight; }
};
