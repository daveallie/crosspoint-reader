#pragma once
#include "Screen.h"

class SleepScreen final : public Screen {
 public:
  explicit SleepScreen(EpdRenderer* renderer) : Screen(renderer) {}
  void onEnter() override;
};
