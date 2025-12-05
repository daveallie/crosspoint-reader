#include "EpubReaderScreen.h"

#include <EpdRenderer.h>
#include <SD.h>

#include "Battery.h"

constexpr int PAGES_PER_REFRESH = 10;
constexpr unsigned long SKIP_CHAPTER_MS = 700;

void EpubReaderScreen::taskTrampoline(void* param) {
  auto* self = static_cast<EpubReaderScreen*>(param);
  self->displayTaskLoop();
}

void EpubReaderScreen::onEnter() {
  sectionMutex = xSemaphoreCreateMutex();

  epub->setupCacheDir();

  // TODO: Move this to a state object
  if (SD.exists((epub->getCachePath() + "/progress.bin").c_str())) {
    File f = SD.open((epub->getCachePath() + "/progress.bin").c_str());
    uint8_t data[4];
    f.read(data, 4);
    currentSpineIndex = data[0] + (data[1] << 8);
    nextPageNumber = data[2] + (data[3] << 8);
    Serial.printf("Loaded cache: %d, %d\n", currentSpineIndex, nextPageNumber);
    f.close();
  }

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&EpubReaderScreen::taskTrampoline, "EpubReaderScreenTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void EpubReaderScreen::onExit() {
  vTaskDelete(displayTaskHandle);
  displayTaskHandle = nullptr;
  xSemaphoreTake(sectionMutex, portMAX_DELAY);
  vSemaphoreDelete(sectionMutex);
  sectionMutex = nullptr;
}

void EpubReaderScreen::handleInput(const Input input) {
  if (input.button == VOLUME_UP || input.button == VOLUME_DOWN || input.button == LEFT || input.button == RIGHT) {
    const bool skipChapter = input.pressTime > SKIP_CHAPTER_MS;

    // No current section, attempt to rerender the book
    if (!section) {
      updateRequired = true;
      return;
    }

    if ((input.button == VOLUME_UP || input.button == LEFT) && skipChapter) {
      nextPageNumber = 0;
      currentSpineIndex--;
      delete section;
      section = nullptr;
    } else if ((input.button == VOLUME_DOWN || input.button == RIGHT) && skipChapter) {
      nextPageNumber = 0;
      currentSpineIndex++;
      delete section;
      section = nullptr;
    } else if (input.button == VOLUME_UP || input.button == LEFT) {
      if (section->currentPage > 0) {
        section->currentPage--;
      } else {
        xSemaphoreTake(sectionMutex, portMAX_DELAY);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        delete section;
        section = nullptr;
        xSemaphoreGive(sectionMutex);
      }
    } else if (input.button == VOLUME_DOWN || input.button == RIGHT) {
      if (section->currentPage < section->pageCount - 1) {
        section->currentPage++;
      } else {
        xSemaphoreTake(sectionMutex, portMAX_DELAY);
        nextPageNumber = 0;
        currentSpineIndex++;
        delete section;
        section = nullptr;
        xSemaphoreGive(sectionMutex);
      }
    }

    updateRequired = true;
  } else if (input.button == BACK) {
    onGoHome();
  }
}

void EpubReaderScreen::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      renderPage();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void EpubReaderScreen::renderPage() {
  if (!epub) {
    return;
  }

  if (currentSpineIndex >= epub->getSpineItemsCount() || currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }

  xSemaphoreTake(sectionMutex, portMAX_DELAY);
  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex);
    Serial.printf("Loading file: %s, index: %d\n", filepath.c_str(), currentSpineIndex);
    section = new Section(epub, currentSpineIndex, renderer);
    if (!section->hasCache()) {
      Serial.println("Cache not found, building...");

      {
        const int textWidth = renderer->getTextWidth("Indexing...");
        constexpr int margin = 20;
        const int x = (renderer->getPageWidth() - textWidth - margin * 2) / 2;
        constexpr int y = 50;
        const int w = textWidth + margin * 2;
        const int h = renderer->getLineHeight() + margin * 2;
        renderer->fillRect(x, y, w, h, 0);
        renderer->drawText(x + margin, y + margin, "Indexing...");
        renderer->drawRect(x + 5, y + 5, w - 10, h - 10);
        renderer->flushArea(x, y, w, h);
      }

      section->setupCacheDir();
      if (!section->persistPageDataToSD()) {
        Serial.println("Failed to persist page data to SD");
        free(section);
        section = nullptr;
        xSemaphoreGive(sectionMutex);
        return;
      }
    } else {
      Serial.println("Cache found, skipping build...");
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }
  }

  renderer->clearScreen();
  section->renderPage();
  renderStatusBar();
  if (pagesUntilFullRefresh <= 1) {
    renderer->flushDisplay(false);
    pagesUntilFullRefresh = PAGES_PER_REFRESH;
  } else {
    renderer->flushDisplay();
    pagesUntilFullRefresh--;
  }

  File f = SD.open((epub->getCachePath() + "/progress.bin").c_str(), FILE_WRITE);
  uint8_t data[4];
  data[0] = currentSpineIndex & 0xFF;
  data[1] = (currentSpineIndex >> 8) & 0xFF;
  data[2] = section->currentPage & 0xFF;
  data[3] = (section->currentPage >> 8) & 0xFF;
  f.write(data, 4);
  f.close();

  xSemaphoreGive(sectionMutex);
}

void EpubReaderScreen::renderStatusBar() const {
  const auto pageWidth = renderer->getPageWidth();

  const std::string progress = std::to_string(section->currentPage + 1) + " / " + std::to_string(section->pageCount);
  const auto progressTextWidth = renderer->getSmallTextWidth(progress.c_str());
  renderer->drawSmallText(pageWidth - progressTextWidth, 765, progress.c_str());

  const uint16_t percentage = battery.readPercentage();
  const auto percentageText = std::to_string(percentage) + "%";
  const auto percentageTextWidth = renderer->getSmallTextWidth(percentageText.c_str());
  renderer->drawSmallText(20, 765, percentageText.c_str());

  // 1 column on left, 2 columns on right, 5 columns of battery body
  constexpr int batteryWidth = 15;
  constexpr int batteryHeight = 10;
  constexpr int x = 0;
  constexpr int y = 772;

  // Top line
  renderer->drawLine(x, y, x + batteryWidth - 4, y);
  // Bottom line
  renderer->drawLine(x, y + batteryHeight - 1, x + batteryWidth - 4, y + batteryHeight - 1);
  // Left line
  renderer->drawLine(x, y, x, y + batteryHeight - 1);
  // Battery end
  renderer->drawLine(x + batteryWidth - 4, y, x + batteryWidth - 4, y + batteryHeight - 1);
  renderer->drawLine(x + batteryWidth - 3, y + 2, x + batteryWidth - 3, y + batteryHeight - 3);
  renderer->drawLine(x + batteryWidth - 2, y + 2, x + batteryWidth - 2, y + batteryHeight - 3);
  renderer->drawLine(x + batteryWidth - 1, y + 2, x + batteryWidth - 1, y + batteryHeight - 3);

  // The +1 is to round up, so that we always fill at least one pixel
  int filledWidth = percentage * (batteryWidth - 5) / 100 + 1;
  if (filledWidth > batteryWidth - 5) {
    filledWidth = batteryWidth - 5;  // Ensure we don't overflow
  }
  renderer->fillRect(x + 1, y + 1, filledWidth, batteryHeight - 2);

  // Page width minus existing content with 30px padding on each side
  const int leftMargin = 20 + percentageTextWidth + 30;
  const int rightMargin = progressTextWidth + 30;
  const int availableTextWidth = pageWidth - leftMargin - rightMargin;
  const auto tocItem = epub->getTocItem(epub->getTocIndexForSpineIndex(currentSpineIndex));
  auto title = tocItem.title;
  int titleWidth = renderer->getSmallTextWidth(title.c_str());
  while (titleWidth > availableTextWidth) {
    title = title.substr(0, title.length() - 8) + "...";
    titleWidth = renderer->getSmallTextWidth(title.c_str());
  }

  renderer->drawSmallText(leftMargin + (availableTextWidth - titleWidth) / 2, 765, title.c_str());
}
