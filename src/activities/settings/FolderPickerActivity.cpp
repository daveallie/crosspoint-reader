#include "FolderPickerActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr unsigned long GO_HOME_MS = 1000;
}  // namespace

void sortFolderList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    return lexicographical_compare(
        begin(str1), end(str1), begin(str2), end(str2),
        [](const char& char1, const char& char2) { return tolower(char1) < tolower(char2); });
  });
}

void FolderPickerActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FolderPickerActivity*>(param);
  self->displayTaskLoop();
}

void FolderPickerActivity::loadFolders() {
  folders.clear();
  selectorIndex = 0;

  // Add option to select current folder
  folders.emplace_back("[Select This Folder]");

  auto root = SdMan.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[128];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      folders.emplace_back(std::string(name) + "/");
    }
    file.close();
  }
  root.close();
  // Sort only the actual folders (skip the first item which is "[Select This Folder]")
  if (folders.size() > 1) {
    std::vector<std::string> actualFolders(folders.begin() + 1, folders.end());
    sortFolderList(actualFolders);
    std::copy(actualFolders.begin(), actualFolders.end(), folders.begin() + 1);
  }
}

void FolderPickerActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  entryTime = millis();

  loadFolders();
  selectorIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&FolderPickerActivity::taskTrampoline, "FolderPickerActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FolderPickerActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  folders.clear();
}

void FolderPickerActivity::loop() {
  // Ignore button presses for 200ms after entry to avoid processing the button that opened this activity
  if (millis() - entryTime < 200) {
    return;
  }

  // Long press BACK (1s+) goes to root folder
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS) {
    if (basepath != "/") {
      basepath = "/";
      loadFolders();
      updateRequired = true;
    }
    return;
  }

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (folders.empty()) {
      return;
    }

    if (selectorIndex == 0) {
      // "[Select This Folder]" option selected
      onSelect(basepath);
    } else if (selectorIndex < folders.size()) {
      // Navigate into the selected folder
      if (basepath.back() != '/') basepath += "/";
      basepath += folders[selectorIndex].substr(0, folders[selectorIndex].length() - 1);
      loadFolders();
      updateRequired = true;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or cancel if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFolders();
        updateRequired = true;
      } else {
        onCancel();
      }
    }
  } else if (prevReleased && !folders.empty()) {
    selectorIndex = (selectorIndex + folders.size() - 1) % folders.size();
    updateRequired = true;
  } else if (nextReleased && !folders.empty()) {
    selectorIndex = (selectorIndex + 1) % folders.size();
    updateRequired = true;
  }
}

void FolderPickerActivity::displayTaskLoop() {
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

void FolderPickerActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Choose Default Folder", true, BOLD);

  // Display current path
  auto truncatedPath = renderer.truncatedText(SMALL_FONT_ID, basepath.c_str(), pageWidth - 40);
  renderer.drawText(SMALL_FONT_ID, 20, 35, truncatedPath.c_str());

  // Help text
  const auto labels = mappedInput.mapLabels("« Cancel", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (folders.empty()) {
    renderer.drawText(UI_10_FONT_ID, 20, 60, "No subfolders. Press Select to use this folder.");
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
  renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);
  for (int i = pageStartIndex; i < folders.size() && i < pageStartIndex + PAGE_ITEMS; i++) {
    auto item = renderer.truncatedText(UI_10_FONT_ID, folders[i].c_str(), renderer.getScreenWidth() - 40);
    renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(), i != selectorIndex);
  }

  renderer.displayBuffer();
}
