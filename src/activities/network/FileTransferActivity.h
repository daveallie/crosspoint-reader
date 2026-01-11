#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "ProtocolSelectionActivity.h"
#include "activities/ActivityWithSubactivity.h"
#include "network/CrossPointFtpServer.h"
#include "network/CrossPointWebServer.h"

// File transfer activity states
enum class FileTransferActivityState {
  PROTOCOL_SELECTION,  // Choosing between HTTP and FTP
  MODE_SELECTION,      // Choosing between Join Network and Create Hotspot
  WIFI_SELECTION,      // WiFi selection subactivity is active (for Join Network mode)
  AP_STARTING,         // Starting Access Point mode
  SERVER_RUNNING,      // Server is running and handling requests
  SHUTTING_DOWN        // Shutting down server and WiFi
};

/**
 * FileTransferActivity is the entry point for file transfer functionality.
 * It allows users to choose between HTTP and FTP protocols for file transfer.
 */
class FileTransferActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  FileTransferActivityState state = FileTransferActivityState::PROTOCOL_SELECTION;
  const std::function<void()> onGoBack;

  // Selected protocol
  FileTransferProtocol selectedProtocol = FileTransferProtocol::HTTP;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;

  // Servers - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;
  std::unique_ptr<CrossPointFtpServer> ftpServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void renderServerRunning() const;

  void onProtocolSelected(FileTransferProtocol protocol);
  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startServer();
  void stopServer();

 public:
  explicit FileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("FileTransfer", renderer, mappedInput), onGoBack(onGoBack) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return (webServer && webServer->isRunning()) || (ftpServer && ftpServer->running()); }
  bool preventAutoSleep() override {
    return (webServer && webServer->isRunning()) || (ftpServer && ftpServer->running());
  }
};
