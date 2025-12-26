#pragma once

#include <InputManager.h>

#include "CrossPointSettings.h"

class FrontButtonMapper {
 public:
  enum class Button { Back, Confirm, Previous, Next };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit FrontButtonMapper(InputManager& inputManager) : inputManager(inputManager) {}

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  unsigned long getAnyButtonHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

 private:
  InputManager& inputManager;
  decltype(InputManager::BTN_BACK) mapButton(Button button) const;
};
