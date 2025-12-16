#pragma once
#include <memory>

#include "Screen.h"
#include "server/UploadServer.h"

class UploadFileScreen final : public Screen {
  enum UploadStatus { Idle, InProgress, Complete };

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::unique_ptr<UploadServer> uploadServer = nullptr;
  size_t currentUploadTotalSize = 0;
  size_t currentUploadCompleteSize = 0;
  String currentUploadFilename = "";
  UploadStatus currentUploadStatus = Idle;
  bool updateRequired = false;
  const std::function<void()> onGoHome;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void onFileUploadStart(AsyncWebServerRequest* request, const String& filename);
  void onFileUploadPart(AsyncWebServerRequest* request, const uint8_t* data, size_t len);
  void onFileUploadEnd(AsyncWebServerRequest* request);

 public:
  explicit UploadFileScreen(GfxRenderer& renderer, InputManager& inputManager, const std::function<void()>& onGoHome)
      : Screen(renderer, inputManager), onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void handleInput() override;
};
