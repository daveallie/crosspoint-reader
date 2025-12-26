#include "FrontButtonMapper.h"

decltype(InputManager::BTN_BACK) FrontButtonMapper::mapButton(const Button button) const {
  const bool swapped = SETTINGS.swapFrontButtons;

  if (!swapped) {
    switch (button) {
      case Button::Back:
        return InputManager::BTN_BACK;
      case Button::Confirm:
        return InputManager::BTN_CONFIRM;
      case Button::Previous:
        return InputManager::BTN_LEFT;
      case Button::Next:
        return InputManager::BTN_RIGHT;
    }
  }

  switch (button) {
    case Button::Back:
      return InputManager::BTN_LEFT;
    case Button::Confirm:
      return InputManager::BTN_RIGHT;
    case Button::Previous:
      return InputManager::BTN_BACK;
    case Button::Next:
      return InputManager::BTN_CONFIRM;
  }

  return InputManager::BTN_BACK;
}

bool FrontButtonMapper::wasPressed(const Button button) const { return inputManager.wasPressed(mapButton(button)); }

bool FrontButtonMapper::wasReleased(const Button button) const { return inputManager.wasReleased(mapButton(button)); }

bool FrontButtonMapper::isPressed(const Button button) const { return inputManager.isPressed(mapButton(button)); }

unsigned long FrontButtonMapper::getAnyButtonHeldTime() const { return inputManager.getHeldTime(); }

FrontButtonMapper::Labels FrontButtonMapper::mapLabels(const char* back, const char* confirm, const char* previous,
                                                       const char* next) const {
  if (!SETTINGS.swapFrontButtons) {
    return {back, confirm, previous, next};
  }

  return {previous, next, back, confirm};
}
