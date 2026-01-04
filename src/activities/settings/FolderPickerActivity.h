#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class FolderPickerActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::string basepath = "/";
  std::vector<std::string> folders;
  int selectorIndex = 0;
  bool updateRequired = false;
  unsigned long entryTime = 0;
  const std::function<void(const std::string&)> onSelect;
  const std::function<void()> onCancel;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void loadFolders();

 public:
  explicit FolderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void(const std::string&)>& onSelect,
                                const std::function<void()>& onCancel, std::string initialPath = "/")
      : Activity("FolderPicker", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)),
        onSelect(onSelect),
        onCancel(onCancel) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
