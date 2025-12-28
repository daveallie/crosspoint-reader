#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

enum FileType {
  F_DIRECTORY = 0,
  F_EPUB,
  F_TXT,
  F_BMP,
  F_FILE
};

struct FileInfo {
    std::string name;
    std::string basename;
    FileType type;
    std::string thumbPath;
};

class GridBrowserActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  TaskHandle_t loadThumbsTaskHandle = nullptr;
  SemaphoreHandle_t loadThumbsMutex = nullptr;
  std::string basepath = "/";
  std::vector<FileInfo> files;
  int selectorIndex = 0;
  int previousSelectorIndex = -1;
  int page = 0;
  bool updateRequired = false;
  bool renderRequired = false;
  bool thumbsLoadingRequired = false;
  const std::function<void(const std::string&)> onSelect;
  const std::function<void()> onGoHome;

  static void displayTaskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  static void loadThumbsTaskTrampoline(void* param);
  void render(bool clear) const;
  void update(bool render) const;
  void loadFiles();
  void drawSelectionRectangle(int tileIndex, bool black) const;
  std::string loadEpubThumb(std::string path);
  void loadThumbsTaskLoop();
  void loadThumbs();
  void onPageChanged();

 public:
  explicit GridBrowserActivity(GfxRenderer& renderer, InputManager& inputManager,
                              const std::function<void(const std::string&)>& onSelect,
                              const std::function<void()>& onGoHome,
                              std::string initialPath = "/")
      : Activity("FileSelection", renderer, inputManager), 
      onSelect(onSelect), 
      onGoHome(onGoHome),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
 private:
  static void sortFileList(std::vector<FileInfo>& strs);
};
