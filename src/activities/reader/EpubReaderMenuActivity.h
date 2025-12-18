#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../Activity.h"

class EpubReaderMenuActivity final : public Activity {
 public:
  enum MenuOption { CHAPTERS, FOOTNOTES };

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectorIndex = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void(MenuOption option)> onSelectOption;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();

 public:
  explicit EpubReaderMenuActivity(GfxRenderer& renderer, InputManager& inputManager,
                                  const std::function<void()>& onGoBack,
                                  const std::function<void(MenuOption option)>& onSelectOption)
      : Activity(renderer, inputManager), onGoBack(onGoBack), onSelectOption(onSelectOption) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
