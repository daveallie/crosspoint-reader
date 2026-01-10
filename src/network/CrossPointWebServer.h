#pragma once

#include <SDCardManager.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  CrossPointWebServer();
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient() const;

  // Check if server is running
  bool isRunning() const { return running; }

  // Get the port number
  uint16_t getPort() const { return port; }

  // Upload status getters (thread-safe)
  bool isUploading() const;
  String getCurrentUploadFile() const;
  float getCurrentUploadSpeed() const;  // KB/s
  uint8_t getUploadProgress() const;    // 0-100%

 private:
  std::unique_ptr<WebServer> server = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;

  // Upload state (protected by mutex)
  mutable SemaphoreHandle_t uploadMutex = nullptr;
  mutable FsFile uploadFile;
  mutable String uploadFileName;
  mutable String uploadPath;
  mutable size_t uploadSize = 0;
  mutable size_t uploadTotalExpected = 0;
  mutable bool uploadSuccess = false;
  mutable String uploadError;
  mutable bool uploadInProgress = false;
  mutable float uploadSpeedKBps = 0.0f;
  mutable unsigned long uploadStartTime = 0;
  mutable unsigned long lastSpeedCalcTime = 0;
  mutable size_t lastSpeedCalcSize = 0;
  mutable bool cpuBoosted = false;

  // Diagnostic counters
  mutable unsigned long totalWriteTimeMs = 0;
  mutable size_t writeCount = 0;

  // CPU frequency management for upload performance
  void boostCPU() const;
  void restoreCPU() const;

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleUpload() const;
  void handleUploadPost() const;
  void handleCreateFolder() const;
  void handleDelete() const;
};
