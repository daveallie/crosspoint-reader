#pragma once
#include <Epub.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../Activity.h"
#include "EpubReaderFootnotesActivity.h"

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::unique_ptr<Activity> subAcitivity = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  FootnotesData currentPageFootnotes;

  int savedSpineIndex = -1;
  int savedPageNumber = -1;
  bool isViewingFootnote = false;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();
  void renderContents(std::unique_ptr<Page> p);
  void renderStatusBar() const;

  // Footnote navigation methods
  void navigateToHref(const char* href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, InputManager& inputManager, std::unique_ptr<Epub> epub,
                              const std::function<void()>& onGoBack)
      : Activity(renderer, inputManager), epub(std::move(epub)), onGoBack(onGoBack) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
