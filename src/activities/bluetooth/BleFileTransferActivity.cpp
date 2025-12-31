#include "BleFileTransferActivity.h"

#include <GfxRenderer.h>
#include <qrcode.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "fontIds.h"

namespace {
constexpr const char* BLE_DEVICE_NAME = "CrossPoint-Reader";
constexpr int LINE_SPACING = 28;
}  // namespace

void BleFileTransferActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BleFileTransferActivity*>(param);
  self->displayTaskLoop();
}

void BleFileTransferActivity::onEnter() {
  Activity::onEnter();

  Serial.printf("[%lu] [BLEACT] [MEM] Free heap at onEnter: %d bytes\n", millis(), ESP.getFreeHeap());

  renderingMutex = xSemaphoreCreateMutex();

  // Reset state
  state = BleActivityState::STARTING;
  lastConnectedCount = 0;
  lastUpdateTime = millis();
  updateRequired = true;

  xTaskCreate(&BleFileTransferActivity::taskTrampoline, "BleActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // Check if WiFi is active (mutual exclusion)
  // Note: We check SETTINGS.bluetoothEnabled in the settings toggle,
  // but this is a safety check in case WiFi was started after BLE was enabled

  Serial.printf("[%lu] [BLEACT] Starting BLE service...\n", millis());

  // Create and start BLE service
  bleService.reset(new BleFileTransfer());
  if (bleService->begin(BLE_DEVICE_NAME)) {
    state = BleActivityState::RUNNING;
    Serial.printf("[%lu] [BLEACT] BLE service started successfully\n", millis());
  } else {
    Serial.printf("[%lu] [BLEACT] ERROR: Failed to start BLE service\n", millis());
    bleService.reset();
    onGoBack();
    return;
  }

  updateRequired = true;
}

void BleFileTransferActivity::onExit() {
  Activity::onExit();

  Serial.printf("[%lu] [BLEACT] [MEM] Free heap at onExit start: %d bytes\n", millis(), ESP.getFreeHeap());

  state = BleActivityState::SHUTTING_DOWN;

  // Stop the BLE service
  if (bleService) {
    Serial.printf("[%lu] [BLEACT] Stopping BLE service...\n", millis());
    bleService->stop();
    bleService.reset();
    Serial.printf("[%lu] [BLEACT] BLE service stopped\n", millis());
  }

  // Small delay to let BLE cleanup complete
  delay(200);

  Serial.printf("[%lu] [BLEACT] [MEM] Free heap after BLE cleanup: %d bytes\n", millis(), ESP.getFreeHeap());

  // Acquire mutex before deleting task
  Serial.printf("[%lu] [BLEACT] Acquiring rendering mutex before task deletion...\n", millis());
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  // Delete the display task
  Serial.printf("[%lu] [BLEACT] Deleting display task...\n", millis());
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
    Serial.printf("[%lu] [BLEACT] Display task deleted\n", millis());
  }

  // Delete the mutex
  Serial.printf("[%lu] [BLEACT] Deleting mutex...\n", millis());
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  Serial.printf("[%lu] [BLEACT] Mutex deleted\n", millis());

  Serial.printf("[%lu] [BLEACT] [MEM] Free heap at onExit end: %d bytes\n", millis(), ESP.getFreeHeap());
}

void BleFileTransferActivity::loop() {
  // Handle exit on Back button
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoBack();
    return;
  }

  // Check for connection count changes
  if (bleService && state == BleActivityState::RUNNING) {
    const uint32_t currentConnectedCount = bleService->getConnectedCount();
    if (currentConnectedCount != lastConnectedCount) {
      lastConnectedCount = currentConnectedCount;
      updateRequired = true;
      Serial.printf("[%lu] [BLEACT] Connection count changed: %u\n", millis(), currentConnectedCount);
    }

    // Periodic update every 5 seconds to show that we're still alive
    if (millis() - lastUpdateTime > 5000) {
      lastUpdateTime = millis();
      updateRequired = true;
    }
  }
}

void BleFileTransferActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void BleFileTransferActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Bluetooth File Transfer", true, BOLD);

  if (state == BleActivityState::RUNNING) {
    int startY = 65;

    // Show device name
    std::string deviceInfo = "Device: ";
    deviceInfo += BLE_DEVICE_NAME;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, deviceInfo.c_str(), true, BOLD);

    // Show connection status
    const uint32_t connectedCount = bleService ? bleService->getConnectedCount() : 0;
    std::string statusText;
    if (connectedCount == 0) {
      statusText = "Status: Waiting for connection...";
    } else if (connectedCount == 1) {
      statusText = "Status: 1 device connected";
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "Status: %u devices connected", connectedCount);
      statusText = buf;
    }
    renderer.drawCenteredText(UI_10_FONT_ID, startY + LINE_SPACING, statusText.c_str());

    // Instructions
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 3,
                              "1. Open a Bluetooth LE scanner app");
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 4,
                              "   on your phone or computer");
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 5,
                              "2. Connect to 'CrossPoint-Reader'");
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 6,
                              "3. Browse files and transfer data");

    // Service info
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 8,
                              "BLE GATT Service Active");
    renderer.drawCenteredText(SMALL_FONT_ID, startY + LINE_SPACING * 9,
                              "File List | Data Transfer | Control");

    // Memory info
    char memBuf[64];
    snprintf(memBuf, sizeof(memBuf), "Free RAM: %d bytes", ESP.getFreeHeap());
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 60, memBuf);

  } else if (state == BleActivityState::STARTING) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, "Starting Bluetooth...", true, BOLD);
  }

  const auto labels = mappedInput.mapLabels("« Exit", "", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
