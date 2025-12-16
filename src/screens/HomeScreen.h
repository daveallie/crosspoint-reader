#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "Screen.h"

class HomeScreen final : public Screen {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectorIndex = 0;
  bool updateRequired = false;
  const std::function<void()> onFileSelectionOpen;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onUploadFileOpen;

  static constexpr int menuItemCount = 3;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  explicit HomeScreen(GfxRenderer& renderer, InputManager& inputManager,
                      const std::function<void()>& onFileSelectionOpen, const std::function<void()>& onSettingsOpen,
                      const std::function<void()>& onUploadFileOpen)
      : Screen(renderer, inputManager),
        onFileSelectionOpen(onFileSelectionOpen),
        onSettingsOpen(onSettingsOpen),
        onUploadFileOpen(onUploadFileOpen) {}
  void onEnter() override;
  void onExit() override;
  void handleInput() override;
};
