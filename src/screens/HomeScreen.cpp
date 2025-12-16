#include "HomeScreen.h"

#include <GfxRenderer.h>
#include <SD.h>

#include "config.h"

void HomeScreen::taskTrampoline(void* param) {
  auto* self = static_cast<HomeScreen*>(param);
  self->displayTaskLoop();
}

void HomeScreen::onEnter() {
  renderingMutex = xSemaphoreCreateMutex();

  selectorIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeScreen::taskTrampoline, "HomeScreenTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void HomeScreen::onExit() {
  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void HomeScreen::handleInput() {
  const bool prevPressed =
      inputManager.wasPressed(InputManager::BTN_UP) || inputManager.wasPressed(InputManager::BTN_LEFT);
  const bool nextPressed =
      inputManager.wasPressed(InputManager::BTN_DOWN) || inputManager.wasPressed(InputManager::BTN_RIGHT);

  if (inputManager.wasPressed(InputManager::BTN_CONFIRM)) {
    if (selectorIndex == 0) {
      onFileSelectionOpen();
    } else if (selectorIndex == 1) {
      onUploadFileOpen();
    } else if (selectorIndex == 2) {
      onSettingsOpen();
    }
  } else if (prevPressed) {
    selectorIndex = (selectorIndex + menuItemCount - 1) % menuItemCount;
    updateRequired = true;
  } else if (nextPressed) {
    selectorIndex = (selectorIndex + 1) % menuItemCount;
    updateRequired = true;
  }
}

void HomeScreen::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void HomeScreen::render() const {
  renderer.clearScreen();

  const auto pageWidth = GfxRenderer::getScreenWidth();
  renderer.drawCenteredText(READER_FONT_ID, 10, "CrossPoint Reader", true, BOLD);

  // Draw selection
  renderer.fillRect(0, 60 + selectorIndex * 30 + 2, pageWidth - 1, 30);
  renderer.drawText(UI_FONT_ID, 20, 60, "Read", selectorIndex != 0);
  renderer.drawText(UI_FONT_ID, 20, 90, "Upload", selectorIndex != 1);
  renderer.drawText(UI_FONT_ID, 20, 120, "Settings", selectorIndex != 2);
  renderer.displayBuffer();
}
