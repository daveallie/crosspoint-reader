#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "Screen.h"

class FileSelectionScreen final : public Screen {
  TaskHandle_t displayTaskHandle = nullptr;
  std::string basepath = "/";
  std::vector<std::string> files;
  int selectorIndex = 0;
  bool updateRequired = false;
  const std::function<void(const std::string&)> onSelect;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void loadFiles();

 public:
  explicit FileSelectionScreen(EpdRenderer* renderer, const std::function<void(const std::string&)>& onSelect)
      : Screen(renderer), onSelect(onSelect) {}
  void onEnter() override;
  void onExit() override;
  void handleInput(Input input) override;
};
