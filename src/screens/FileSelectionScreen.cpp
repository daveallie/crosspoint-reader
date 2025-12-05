#include "FileSelectionScreen.h"

#include <EpdRenderer.h>
#include <SD.h>

void FileSelectionScreen::taskTrampoline(void* param) {
  auto* self = static_cast<FileSelectionScreen*>(param);
  self->displayTaskLoop();
}

void FileSelectionScreen::loadFiles() {
  files.clear();
  selectorIndex = 0;
  auto root = SD.open(basepath.c_str());
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    auto filename = std::string(file.name());
    if (filename[0] == '.') {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(filename + "/");
    } else if (filename.substr(filename.length() - 5) == ".epub") {
      files.emplace_back(filename);
    }
    file.close();
  }
  root.close();
}

void FileSelectionScreen::onEnter() {
  basepath = "/";
  loadFiles();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&FileSelectionScreen::taskTrampoline, "FileSelectionScreenTask",
              1024,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FileSelectionScreen::onExit() {
  vTaskDelete(displayTaskHandle);
  displayTaskHandle = nullptr;
}

void FileSelectionScreen::handleInput(const Input input) {
  if (input.button == VOLUME_DOWN || input.button == RIGHT) {
    selectorIndex = (selectorIndex + 1) % files.size();
    updateRequired = true;
  } else if (input.button == VOLUME_UP || input.button == LEFT) {
    selectorIndex = (selectorIndex + files.size() - 1) % files.size();
    updateRequired = true;
  } else if (input.button == CONFIRM) {
    if (files.empty()) {
      return;
    }

    if (files[selectorIndex].back() == '/') {
      if (basepath.back() != '/') basepath += "/";
      basepath += files[selectorIndex].substr(0, files[selectorIndex].length() - 1);
      loadFiles();
      updateRequired = true;
    } else {
      onSelect(basepath + files[selectorIndex]);
    }
  } else if (input.button == BACK && basepath != "/") {
    basepath = basepath.substr(0, basepath.rfind('/'));
    if (basepath.empty()) basepath = "/";
    loadFiles();
    updateRequired = true;
  }
}

void FileSelectionScreen::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FileSelectionScreen::render() const {
  renderer->clearScreen();

  const auto pageWidth = renderer->getPageWidth();
  const auto titleWidth = renderer->getTextWidth("CrossPoint Reader", BOLD);
  renderer->drawText((pageWidth - titleWidth) / 2, 0, "CrossPoint Reader", 1, BOLD);

  if (files.empty()) {
    renderer->drawSmallText(50, 50, "No EPUBs found");
  } else {
    // Draw selection
    renderer->fillRect(0, 50 + selectorIndex * 20 + 2, pageWidth - 1, 20);

    for (size_t i = 0; i < files.size(); i++) {
      const auto file = files[i];
      renderer->drawSmallText(50, 50 + i * 20, file.c_str(), i == selectorIndex ? 0 : 1);
    }
  }

  renderer->flushDisplay();
}
