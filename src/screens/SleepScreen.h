#pragma once
#include "Screen.h"

class CrossPointSettings;

class SleepScreen final : public Screen {
 public:
  explicit SleepScreen(GfxRenderer& renderer, InputManager& inputManager)
      : Screen(renderer, inputManager) {}
  void onEnter() override;
};
