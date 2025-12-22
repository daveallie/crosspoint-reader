#include "GridBrowserActivity.h"

#include <GfxRenderer.h>
#include <SD.h>

#include "config.h"
#include "../../images/FolderIcon.h"

namespace {
constexpr int PAGE_ITEMS = 12;
constexpr int SKIP_PAGE_MS = 700;
constexpr int TILE_W = 135;
constexpr int TILE_H = 200;
constexpr int TILE_PADDING = 5;
constexpr int THUMB_W = 90;
constexpr int THUMB_H = 120;
constexpr int TILE_TEXT_H = 60;
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

void GridBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<GridBrowserActivity*>(param);
  self->displayTaskLoop();
}

void GridBrowserActivity::loadFiles() {
  files.clear();
  selectorIndex = 0;
  auto root = SD.open(basepath.c_str());
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    const std::string filename = std::string(file.name());
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(FileInfo{ filename, filename, F_DIRECTORY });
    } else {
      FileType type = F_FILE;
      size_t dot = filename.find_last_of('.');
      std::string basename = filename;
      if (dot != std::string::npos) {
        std::string ext = filename.substr(dot);
        basename = filename.substr(0, dot); 
        // lowercase ext for case-insensitive compare
        for (char &c : ext) c = (char)tolower(c);
        if (ext == ".epub") {
          type = F_EPUB;
        } else if (ext == ".bmp") {
          type = F_BMP;
        }
      }
      if (type != F_FILE) {
        files.emplace_back(FileInfo{ filename, basename, type });
      }
    }
    file.close();
  }
  root.close();
  Serial.printf("Files loaded\n");
  GridBrowserActivity::sortFileList(files);
  Serial.printf("Files sorted\n");
}

void GridBrowserActivity::onEnter() {
  Serial.printf("Enter grid\n");
  renderingMutex = xSemaphoreCreateMutex();
  
  basepath = "/Dev/Thumbs";
  loadFiles();
  selectorIndex = 0;
  
  // Trigger first update
  updateRequired = true;
  
  xTaskCreate(&GridBrowserActivity::taskTrampoline, "GridFileBrowserTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
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
  files.clear();
}

void GridBrowserActivity::loop() {
  const bool prevReleased = inputManager.wasReleased(InputManager::BTN_UP) || inputManager.wasReleased(InputManager::BTN_LEFT);
  const bool nextReleased = inputManager.wasReleased(InputManager::BTN_DOWN) || inputManager.wasReleased(InputManager::BTN_RIGHT);
  const bool skipPage = inputManager.getHeldTime() > SKIP_PAGE_MS;

  if (inputManager.wasPressed(InputManager::BTN_CONFIRM)) {
    if (files.empty()) {
      return;
    }

    if (basepath.back() != '/') {
      basepath += "/";
    }
    if (files[selectorIndex].type == F_DIRECTORY) {
      // open subfolder
      basepath += files[selectorIndex].name;
      loadFiles();
      updateRequired = true;
    } else {
      onSelect(basepath + files[selectorIndex].name);
    }
  } else if (inputManager.wasPressed(InputManager::BTN_BACK)) {
    if (basepath != "/") {
      basepath = basepath.substr(0, basepath.rfind('/'));
      if (basepath.empty()) basepath = "/";
      loadFiles();
      updateRequired = true;
    } else {
      // At root level, go back home
      onGoHome();
    }
  } else if (prevReleased) {
    if (skipPage) {
      selectorIndex = ((selectorIndex / PAGE_ITEMS - 1) * PAGE_ITEMS + files.size()) % files.size();
    } else {
      selectorIndex = (selectorIndex + files.size() - 1) % files.size();
    }
    updateRequired = true;
  } else if (nextReleased) {
    if (skipPage) {
      selectorIndex = ((selectorIndex / PAGE_ITEMS + 1) * PAGE_ITEMS) % files.size();
    } else {
      selectorIndex = (selectorIndex + 1) % files.size();
    }
    updateRequired = true;
  }
}

void GridBrowserActivity::displayTaskLoop() {
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

void GridBrowserActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  bool hasGeyscaleBitmaps = false;
  
  if (!files.empty()) {
    for (int pass = 0; pass < 3; pass++) {
      if (pass > 0) {
        renderer.clearScreen(0x00);
        if (pass == 1) {
          renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        } else if (pass == 2) {
          renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        }
      }

      const int16_t iconOffsetX = (TILE_W - FOLDERICON_WIDTH) / 2;
      const int16_t iconOffsetY = (TILE_H - TILE_TEXT_H - FOLDERICON_HEIGHT) / 2;
      const int16_t thumbOffsetX = (TILE_W - THUMB_W) / 2;
      const int16_t thumbOffsetY = (TILE_H - TILE_TEXT_H - THUMB_H) / 2;
      for (size_t i = 0; i < min(PAGE_ITEMS, files.size()); i++) {
        const auto file = files[i];
        
        const int16_t tileX = 45 + i % 3 * TILE_W;
        const int16_t tileY = 115 + i / 3 * TILE_H;

        if (pass == 0) {
          Serial.printf("Rendering file %s at (%d, %d)\n", file.name.c_str(), tileX, tileY);
          if (file.type == F_DIRECTORY) {
            renderer.drawImage(FolderIcon, tileX + iconOffsetX, tileY + iconOffsetY, FOLDERICON_WIDTH, FOLDERICON_HEIGHT);
          }
        }

        if (file.type == F_BMP) {
          File bmpFile = SD.open((basepath + "/" + file.name).c_str());
          if (bmpFile) {
            Bitmap bitmap(bmpFile);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              if (bitmap.hasGreyscale()) {
                hasGeyscaleBitmaps = true;
              }
              renderer.drawBitmap(bitmap, tileX + thumbOffsetX, tileY + thumbOffsetY, THUMB_W, THUMB_H);
            }
          }
        }
        
        if (pass == 0) {
          renderer.drawTextInBox(UI_FONT_ID, tileX + TILE_PADDING, tileY + TILE_H - TILE_TEXT_H, TILE_W - 2 * TILE_PADDING, TILE_TEXT_H, file.basename.c_str(), 1); // i != selectorIndex
        }
      }

      if (pass == 0) {
        renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
        if (!hasGeyscaleBitmaps) {
          // we can skip grayscale passes if no bitmaps use it
          break;
        }
      } else if (pass == 1) {
        renderer.copyGrayscaleLsbBuffers();
      } else if (pass == 2) {
        renderer.copyGrayscaleMsbBuffers();
        renderer.displayGrayBuffer();
        renderer.setRenderMode(GfxRenderer::BW);
      }
    }
  }
} 