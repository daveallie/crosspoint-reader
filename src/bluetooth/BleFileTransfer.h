#pragma once

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <functional>
#include <memory>
#include <string>

// BLE File Transfer Service
// Provides file upload/download over Bluetooth Low Energy
// Designed for memory-constrained ESP32-C3 environment
class BleFileTransfer {
 public:
  BleFileTransfer();
  ~BleFileTransfer();

  // Start the BLE service
  bool begin(const std::string& deviceName = "CrossPoint-Reader");

  // Stop the BLE service and free resources
  void stop();

  // Check if service is running
  bool isRunning() const { return running; }

  // Get number of connected clients
  uint32_t getConnectedCount() const;

 private:
  bool running;
  BLEServer* pServer;
  BLEService* pFileService;
  BLECharacteristic* pFileListChar;
  BLECharacteristic* pFileDataChar;
  BLECharacteristic* pControlChar;

  // Server callbacks
  class ServerCallbacks : public BLEServerCallbacks {
   public:
    ServerCallbacks(BleFileTransfer* parent) : parent(parent) {}
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;

   private:
    BleFileTransfer* parent;
  };

  // Control characteristic callbacks
  class ControlCallbacks : public BLECharacteristicCallbacks {
   public:
    ControlCallbacks(BleFileTransfer* parent) : parent(parent) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;

   private:
    BleFileTransfer* parent;
  };

  // File list characteristic callbacks
  class FileListCallbacks : public BLECharacteristicCallbacks {
   public:
    FileListCallbacks(BleFileTransfer* parent) : parent(parent) {}
    void onRead(BLECharacteristic* pCharacteristic) override;

   private:
    BleFileTransfer* parent;
  };

  void handleControlCommand(const std::string& command);
  std::string getFileList();

  std::unique_ptr<ServerCallbacks> serverCallbacks;
  std::unique_ptr<ControlCallbacks> controlCallbacks;
  std::unique_ptr<FileListCallbacks> fileListCallbacks;
};
