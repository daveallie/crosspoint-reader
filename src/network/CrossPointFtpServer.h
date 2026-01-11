#pragma once

// Configure SimpleFTPServer to use SdFat2 instead of SD library
#define DEFAULT_STORAGE_TYPE_ESP32 2  // STORAGE_SDFAT2

#include <SimpleFTPServer.h>

class CrossPointFtpServer {
 private:
  FtpServer* ftpServer = nullptr;
  bool isRunning = false;

 public:
  CrossPointFtpServer() = default;
  ~CrossPointFtpServer();

  // Initialize and start the FTP server
  bool begin();

  // Stop the FTP server
  void stop();

  // Handle FTP client requests (call this regularly in loop)
  void handleClient();

  // Check if server is running
  bool running() const { return isRunning; }
};
