#pragma once

#include <ESPAsyncWebServer.h>
#include <SDCardManager.h>
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
  std::unique_ptr<AsyncWebServer> server = nullptr;
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

  // Diagnostic counters
  mutable unsigned long totalWriteTimeMs = 0;
  mutable size_t writeCount = 0;

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot(AsyncWebServerRequest* request) const;
  void handleNotFound(AsyncWebServerRequest* request) const;
  void handleStatus(AsyncWebServerRequest* request) const;
  void handleFileList(AsyncWebServerRequest* request) const;
  void handleFileListData(AsyncWebServerRequest* request) const;
  void handleUploadRequest(AsyncWebServerRequest* request);
  void handleUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len,
                    bool final);
  void handleCreateFolder(AsyncWebServerRequest* request) const;
  void handleDelete(AsyncWebServerRequest* request) const;
};
