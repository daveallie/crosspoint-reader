#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "bluetooth/BleFileTransfer.h"

enum class BleActivityState {
  STARTING,     // BLE service is starting
  RUNNING,      // BLE service is running and advertising
  SHUTTING_DOWN // Shutting down BLE service
};

/**
 * BleFileTransferActivity manages the BLE file transfer service.
 * It starts the BLE service, displays connection status, and handles cleanup.
 */
class BleFileTransferActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  BleActivityState state = BleActivityState::STARTING;
  const std::function<void()> onGoBack;

  // BLE service - owned by this activity
  std::unique_ptr<BleFileTransfer> bleService;

  // Status tracking
  uint32_t lastConnectedCount = 0;
  unsigned long lastUpdateTime = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  explicit BleFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onGoBack)
      : Activity("BleFileTransfer", renderer, mappedInput), onGoBack(onGoBack) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return false; } // BLE doesn't need fast polling
};
