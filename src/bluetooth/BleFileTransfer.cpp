#include "BleFileTransfer.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

namespace {
// BLE Service UUIDs (custom UUIDs for CrossPoint file transfer)
constexpr const char* SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr const char* FILE_LIST_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr const char* FILE_DATA_UUID = "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e";
constexpr const char* CONTROL_UUID = "d7e72d4c-3f8e-4b4a-9c5d-8e3f7a2b1c9d";

constexpr int BLE_MTU = 512;  // BLE Maximum Transmission Unit
}  // namespace

BleFileTransfer::BleFileTransfer()
    : running(false), pServer(nullptr), pFileService(nullptr), pFileListChar(nullptr), pFileDataChar(nullptr),
      pControlChar(nullptr) {}

BleFileTransfer::~BleFileTransfer() {
  stop();
}

bool BleFileTransfer::begin(const std::string& deviceName) {
  if (running) {
    Serial.printf("[%lu] [BLE] Already running\n", millis());
    return true;
  }

  Serial.printf("[%lu] [BLE] Starting BLE service...\n", millis());
  Serial.printf("[%lu] [BLE] [MEM] Free heap before init: %d bytes\n", millis(), ESP.getFreeHeap());

  // Initialize BLE
  BLEDevice::init(deviceName);

  // Set MTU size for larger transfers
  BLEDevice::setMTU(BLE_MTU);

  // Create BLE Server
  pServer = BLEDevice::createServer();
  if (!pServer) {
    Serial.printf("[%lu] [BLE] ERROR: Failed to create server\n", millis());
    return false;
  }

  serverCallbacks.reset(new ServerCallbacks(this));
  pServer->setCallbacks(serverCallbacks.get());

  // Create File Transfer Service
  pFileService = pServer->createService(SERVICE_UUID);
  if (!pFileService) {
    Serial.printf("[%lu] [BLE] ERROR: Failed to create service\n", millis());
    return false;
  }

  // Create File List Characteristic (READ)
  pFileListChar = pFileService->createCharacteristic(FILE_LIST_UUID, BLECharacteristic::PROPERTY_READ);
  fileListCallbacks.reset(new FileListCallbacks(this));
  pFileListChar->setCallbacks(fileListCallbacks.get());

  // Create File Data Characteristic (READ | WRITE | NOTIFY)
  pFileDataChar =
      pFileService->createCharacteristic(FILE_DATA_UUID, BLECharacteristic::PROPERTY_READ |
                                                              BLECharacteristic::PROPERTY_WRITE |
                                                              BLECharacteristic::PROPERTY_NOTIFY);
  pFileDataChar->addDescriptor(new BLE2902());

  // Create Control Characteristic (WRITE)
  pControlChar = pFileService->createCharacteristic(CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE);
  controlCallbacks.reset(new ControlCallbacks(this));
  pControlChar->setCallbacks(controlCallbacks.get());

  // Start the service
  pFileService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iPhone connections
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  running = true;
  Serial.printf("[%lu] [BLE] Service started successfully\n", millis());
  Serial.printf("[%lu] [BLE] Device name: %s\n", millis(), deviceName.c_str());
  Serial.printf("[%lu] [BLE] [MEM] Free heap after init: %d bytes\n", millis(), ESP.getFreeHeap());

  return true;
}

void BleFileTransfer::stop() {
  if (!running) {
    return;
  }

  Serial.printf("[%lu] [BLE] Stopping BLE service...\n", millis());

  running = false;

  // Stop advertising
  BLEDevice::getAdvertising()->stop();

  // Clean up characteristics
  pFileListChar = nullptr;
  pFileDataChar = nullptr;
  pControlChar = nullptr;

  // Clean up service
  pFileService = nullptr;

  // Clean up server
  pServer = nullptr;

  // Clean up callbacks
  serverCallbacks.reset();
  controlCallbacks.reset();
  fileListCallbacks.reset();

  // Deinitialize BLE
  BLEDevice::deinit(true);

  Serial.printf("[%lu] [BLE] Service stopped\n", millis());
  Serial.printf("[%lu] [BLE] [MEM] Free heap after cleanup: %d bytes\n", millis(), ESP.getFreeHeap());
}

uint32_t BleFileTransfer::getConnectedCount() const {
  if (pServer) {
    return pServer->getConnectedCount();
  }
  return 0;
}

std::string BleFileTransfer::getFileList() {
  // Return a simple JSON-like list of files in the root directory
  // Format: "file1.epub,file2.epub,file3.epub"
  // For a full implementation, this would traverse SD card directories

  // Placeholder implementation - would need to integrate with SDCardManager
  return "example1.epub,example2.epub,example3.epub";
}

void BleFileTransfer::handleControlCommand(const std::string& command) {
  Serial.printf("[%lu] [BLE] Control command: %s\n", millis(), command.c_str());

  // Parse and handle commands
  // Commands could be: "LIST", "GET:filename", "PUT:filename", "DELETE:filename", etc.
  // For a full implementation, this would handle file operations via SDCardManager

  if (command == "LIST") {
    // Refresh file list
    Serial.printf("[%lu] [BLE] Refreshing file list\n", millis());
  } else if (command.rfind("GET:", 0) == 0) {
    std::string filename = command.substr(4);
    Serial.printf("[%lu] [BLE] Request to download: %s\n", millis(), filename.c_str());
    // Would implement file read and send via pFileDataChar notifications
  } else if (command.rfind("PUT:", 0) == 0) {
    std::string filename = command.substr(4);
    Serial.printf("[%lu] [BLE] Request to upload: %s\n", millis(), filename.c_str());
    // Would implement file write from pFileDataChar writes
  }
}

// Server callbacks
void BleFileTransfer::ServerCallbacks::onConnect(BLEServer* pServer) {
  Serial.printf("[%lu] [BLE] Client connected (total: %u)\n", millis(), pServer->getConnectedCount());
}

void BleFileTransfer::ServerCallbacks::onDisconnect(BLEServer* pServer) {
  Serial.printf("[%lu] [BLE] Client disconnected (total: %u)\n", millis(), pServer->getConnectedCount());
  // Restart advertising to allow new connections
  BLEDevice::startAdvertising();
}

// Control callbacks
void BleFileTransfer::ControlCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
  if (value.length() > 0) {
    parent->handleControlCommand(value);
  }
}

// File list callbacks
void BleFileTransfer::FileListCallbacks::onRead(BLECharacteristic* pCharacteristic) {
  std::string fileList = parent->getFileList();
  pCharacteristic->setValue(fileList);
  Serial.printf("[%lu] [BLE] File list requested (%zu bytes)\n", millis(), fileList.length());
}
