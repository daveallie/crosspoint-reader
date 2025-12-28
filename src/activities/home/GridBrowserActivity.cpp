#include "GridBrowserActivity.h"

#include <GfxRenderer.h>
#include <SD.h>
#include <InputManager.h>
#include <Epub.h>

#include "config.h"
#include "../../images/FolderIcon.h"
#include "../util/Window.h"

namespace {
constexpr int PAGE_ITEMS = 9;
constexpr int SKIP_PAGE_MS = 700;
constexpr int TILE_W = 135;
constexpr int TILE_H = 200;
constexpr int TILE_PADDING = 5;
constexpr int THUMB_W = 90;
constexpr int THUMB_H = 120;
constexpr int TILE_TEXT_H = 60;
constexpr int GRID_OFFSET_LEFT = 37;
constexpr int GRID_OFFSET_TOP = 125;
}  // namespace

inline int min(const int a, const int b) { return a < b ? a : b; }

void GridBrowserActivity::sortFileList(std::vector<FileInfo>& strs) {
  std::sort(begin(strs), end(strs), [](const FileInfo& f1, const FileInfo& f2) {
    if (f1.type == F_DIRECTORY && f2.type != F_DIRECTORY) return true;
    if (f1.type != F_DIRECTORY && f2.type == F_DIRECTORY) return false;
    return lexicographical_compare(
        begin(f1.name), end(f1.name), begin(f2.name), end(f2.name),
        [](const char& char1, const char& char2) { return tolower(char1) < tolower(char2); });
  });
}

void GridBrowserActivity::displayTaskTrampoline(void* param) {
  auto* self = static_cast<GridBrowserActivity*>(param);
  self->displayTaskLoop();
}

void GridBrowserActivity::loadThumbsTaskTrampoline(void* param) {
  auto* self = static_cast<GridBrowserActivity*>(param);
  self->loadThumbsTaskLoop();
}

void GridBrowserActivity::loadThumbsTaskLoop() {
  while (true) {
    if (thumbsLoadingRequired) {
      xSemaphoreTake(loadThumbsMutex, portMAX_DELAY);
      loadThumbs();
      xSemaphoreGive(loadThumbsMutex);
      thumbsLoadingRequired = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void GridBrowserActivity::loadThumbs() {
  int thumbsCount = min(PAGE_ITEMS, files.size() - page * PAGE_ITEMS);
  for (int i = 0; i < thumbsCount; i++) {
    const auto file = files[i + page * PAGE_ITEMS];
    if (file.type == F_EPUB) {
      if (file.thumbPath.empty()) {
        Serial.printf("[%lu] Loading thumb for epub: %s\n", millis(), file.name.c_str());
        std::string thumbPath = loadEpubThumb(basepath + "/" + file.name);
        if (!thumbPath.empty()) {
          files[i + page * PAGE_ITEMS].thumbPath = thumbPath;
        }
        renderRequired = true;
        taskYIELD();
      }
    }
  }
}

std::string GridBrowserActivity::loadEpubThumb(std::string path) {
  File file;
  Epub epubFile(path, "/.crosspoint");
  if (!epubFile.load()) {
    Serial.printf("[%lu] Failed to load epub: %s\n", millis(), path.c_str());
    return "";
  }
  if (!epubFile.generateCoverBmp(true)) {
    Serial.printf("[%lu] Failed to generate epub thumb\n", millis());
    return "";
  }
  std::string thumbPath = epubFile.getThumbBmpPath();
  Serial.printf("[%lu] epub has thumb at %s\n", millis(), thumbPath.c_str());
  return thumbPath;
}

void GridBrowserActivity::loadFiles() {
  files.clear();
  selectorIndex = 0;
  previousSelectorIndex = -1;
  page = 0;
  auto root = SD.open(basepath.c_str());
  int count = 0;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    const std::string filename = std::string(file.name());
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(FileInfo{ filename, filename, F_DIRECTORY, "" });
    } else {
      FileType type = F_FILE;
      size_t dot = filename.find_first_of('.');
      std::string basename = filename;
      if (dot != std::string::npos) {
        std::string ext = filename.substr(dot);
        basename = filename.substr(0, dot); 
        // lowercase ext for case-insensitive compare
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ext == ".epub") {
          type = F_EPUB;
        } else if (ext == ".bmp") {
          type = F_BMP;
        }
      }
      if (type != F_FILE) {
        files.emplace_back(FileInfo{ filename, basename, type, "" });
      }
    }
    file.close();
    count++;
  }
  root.close();
  GridBrowserActivity::sortFileList(files);
}

void GridBrowserActivity::onEnter() {
  renderingMutex = xSemaphoreCreateMutex();
  loadThumbsMutex = xSemaphoreCreateMutex();
  
  page = 0;
  loadFiles();
  onPageChanged();
  
  xTaskCreate(&GridBrowserActivity::displayTaskTrampoline, "GridFileBrowserTask",
    8192,               // Stack size
    this,               // Parameters
    2,                  // Priority
    &displayTaskHandle  // Task handle
  );
  xTaskCreate(&GridBrowserActivity::loadThumbsTaskTrampoline, "LoadThumbsTask",
    8192,               // Stack size
    this,               // Parameters
    1,                  // Priority
    &loadThumbsTaskHandle  // Task handle
  ); 
}

void GridBrowserActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  if (loadThumbsTaskHandle) {
    vTaskDelete(loadThumbsTaskHandle);
    loadThumbsTaskHandle = nullptr;
  }
  vSemaphoreDelete(loadThumbsMutex);
  loadThumbsMutex = nullptr;

  files.clear();
}

void GridBrowserActivity::onPageChanged() {
  selectorIndex = 0;
  previousSelectorIndex = -1;
  renderRequired = true;
  thumbsLoadingRequired = true;
}

void GridBrowserActivity::loop() {
  const bool prevReleased = inputManager.wasReleased(InputManager::BTN_UP) || inputManager.wasReleased(InputManager::BTN_LEFT);
  const bool nextReleased = inputManager.wasReleased(InputManager::BTN_DOWN) || inputManager.wasReleased(InputManager::BTN_RIGHT);
  const bool skipPage = inputManager.getHeldTime() > SKIP_PAGE_MS;
  const int selected = selectorIndex + page * PAGE_ITEMS;

  if (inputManager.wasPressed(InputManager::BTN_CONFIRM)) {
    if (files.empty()) {
      return;
    }

    if (basepath.back() != '/') {
      basepath += "/";
    }
    if (files[selected].type == F_DIRECTORY) {
      // open subfolder
      basepath += files[selected].name;
      loadFiles();
      onPageChanged();
    } else {
      onSelect(basepath + files[selected].name);
    }
  } else if (inputManager.wasPressed(InputManager::BTN_BACK)) {
    if (basepath != "/") {
      basepath.resize(basepath.rfind('/'));
      if (basepath.empty()) basepath = "/";
      loadFiles();
      onPageChanged();
    } else {
      // At root level, go back home
      onGoHome();
    }
  } else if (prevReleased) {
    previousSelectorIndex = selectorIndex;
    if (selectorIndex == 0 || skipPage) {
      if (page > 0) {
        page--;
        onPageChanged();
      }
    } else {
      selectorIndex--;
      updateRequired = true;
    }
  } else if (nextReleased) {
    previousSelectorIndex = selectorIndex;
    if (selectorIndex == min(PAGE_ITEMS, files.size() - page * PAGE_ITEMS) - 1 || skipPage) {
      if (page < files.size() / PAGE_ITEMS) {
        page++;
        onPageChanged();
      }
    } else {
      selectorIndex++;
      updateRequired = true;
    }
  }
}

void GridBrowserActivity::displayTaskLoop() {
  while (true) {
    if (renderRequired || updateRequired) {
      bool didRequireRender = renderRequired;
      renderRequired = false;
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render(didRequireRender);
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void GridBrowserActivity::render(bool clear) const {
  if (clear) {
    renderer.clearScreen();
    auto folderName = basepath == "/" ? "SD card" : basepath.substr(basepath.rfind('/') + 1).c_str();
    drawFullscreenWindowFrame(renderer, folderName);
  }
  
  if (!files.empty()) {      
    for (size_t i = 0; i < min(PAGE_ITEMS, files.size() - page * PAGE_ITEMS); i++) {
      const auto file = files[i + page * PAGE_ITEMS];
      
      const int16_t tileX = GRID_OFFSET_LEFT + i % 3 * TILE_W;
      const int16_t tileY = GRID_OFFSET_TOP + i / 3 * TILE_H;

      if (file.type == F_DIRECTORY) {
        constexpr int iconOffsetX = (TILE_W - FOLDERICON_WIDTH) / 2;
        constexpr int iconOffsetY = (TILE_H - TILE_TEXT_H - FOLDERICON_HEIGHT) / 2;
        renderer.drawIcon(FolderIcon, tileX + iconOffsetX, tileY + iconOffsetY, FOLDERICON_WIDTH, FOLDERICON_HEIGHT);
      }

      if (!file.thumbPath.empty()) {
        Serial.printf("Rendering file thumb: %s\n", file.thumbPath.c_str());
        File bmpFile = SD.open(file.thumbPath.c_str());
        if (bmpFile) {
          Bitmap bitmap(bmpFile);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            constexpr int thumbOffsetX = (TILE_W - THUMB_W) / 2;
            constexpr int thumbOffsetY = (TILE_H - TILE_TEXT_H - THUMB_H) / 2;
            renderer.drawBitmap(bitmap, tileX + thumbOffsetX, tileY + thumbOffsetY, THUMB_W, THUMB_H);
          }
        }
      }
      
      renderer.drawTextInBox(UI_FONT_ID, tileX + TILE_PADDING, tileY + TILE_H - TILE_TEXT_H, TILE_W - 2 * TILE_PADDING, TILE_TEXT_H, file.basename.c_str(), true);
    }

    update(false);
    renderer.displayBuffer();
  }
} 

void GridBrowserActivity::drawSelectionRectangle(int tileIndex, bool black) const {
  renderer.drawRoundedRect(GRID_OFFSET_LEFT + tileIndex % 3 * TILE_W, GRID_OFFSET_TOP + tileIndex / 3 * TILE_H, TILE_W, TILE_H, 2, 5, black);
}

void GridBrowserActivity::update(bool render) const {
  // Redraw only changed tiles
  // renderer.clearScreen();
  if (previousSelectorIndex >= 0) {
    drawSelectionRectangle(previousSelectorIndex, false);
  }
  drawSelectionRectangle(selectorIndex, true);
}