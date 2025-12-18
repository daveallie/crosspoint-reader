#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>

#include "config.h"

constexpr int MENU_ITEMS_COUNT = 2;

void EpubReaderMenuActivity::taskTrampoline(void* param) {
  auto* self = static_cast<EpubReaderMenuActivity*>(param);
  self->displayTaskLoop();
}

void EpubReaderMenuActivity::onEnter() {
  renderingMutex = xSemaphoreCreateMutex();
  selectorIndex = 0;

  // Trigger first update
  updateRequired = true;
  xTaskCreate(&EpubReaderMenuActivity::taskTrampoline, "EpubReaderMenuTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void EpubReaderMenuActivity::onExit() {
  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void EpubReaderMenuActivity::loop() {
  const bool prevReleased =
      inputManager.wasReleased(InputManager::BTN_UP) || inputManager.wasReleased(InputManager::BTN_LEFT);
  const bool nextReleased =
      inputManager.wasReleased(InputManager::BTN_DOWN) || inputManager.wasReleased(InputManager::BTN_RIGHT);

  if (inputManager.wasPressed(InputManager::BTN_CONFIRM)) {
    onSelectOption(static_cast<MenuOption>(selectorIndex));
  } else if (inputManager.wasPressed(InputManager::BTN_BACK)) {
    onGoBack();
  } else if (prevReleased) {
    selectorIndex = (selectorIndex + MENU_ITEMS_COUNT - 1) % MENU_ITEMS_COUNT;
    updateRequired = true;
  } else if (nextReleased) {
    selectorIndex = (selectorIndex + 1) % MENU_ITEMS_COUNT;
    updateRequired = true;
  }
}

void EpubReaderMenuActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void EpubReaderMenuActivity::renderScreen() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawCenteredText(READER_FONT_ID, 10, "Menu", true, BOLD);

  const char* menuItems[MENU_ITEMS_COUNT] = {"Go to chapter", "View footnotes"};

  const int startY = 100;
  const int itemHeight = 40;

  for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
    const int y = startY + i * itemHeight;

    // Draw selection indicator
    if (i == selectorIndex) {
      renderer.fillRect(10, y + 2, pageWidth - 20, itemHeight - 4);
      renderer.drawText(UI_FONT_ID, 30, y, menuItems[i], false);
    } else {
      renderer.drawText(UI_FONT_ID, 30, y, menuItems[i], true);
    }
  }

  renderer.displayBuffer();
}
