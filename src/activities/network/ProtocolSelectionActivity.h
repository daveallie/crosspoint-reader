#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../Activity.h"

enum class FileTransferProtocol { HTTP, FTP };

class ProtocolSelectionActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectedIndex = 0;
  bool updateRequired = false;
  const std::function<void(FileTransferProtocol)> onProtocolSelected;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  explicit ProtocolSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void(FileTransferProtocol)>& onProtocolSelected)
      : Activity("ProtocolSelection", renderer, mappedInput), onProtocolSelected(onProtocolSelected) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
