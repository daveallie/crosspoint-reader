#pragma once
#include <string>
#include <utility>

#include "EpdFontFamily.h"
#include "Screen.h"

class FullScreenMessageScreen final : public Screen {
  std::string text;
  EpdFontStyle style;
  bool invert;

 public:
  explicit FullScreenMessageScreen(EpdRenderer* renderer, std::string text, const EpdFontStyle style = REGULAR,
                                   const bool invert = false)
      : Screen(renderer), text(std::move(text)), style(style), invert(invert) {}
  void onEnter() override;
};
