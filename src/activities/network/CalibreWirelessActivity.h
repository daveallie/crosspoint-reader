#pragma once
#include <AsyncTCP.h>
#include <SDCardManager.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>

#include "activities/Activity.h"

/**
 * CalibreWirelessActivity implements Calibre's "wireless device" protocol.
 * This allows Calibre desktop to send books directly to the device over WiFi.
 *
 * Protocol specification sourced from Calibre's smart device driver:
 * https://github.com/kovidgoyal/calibre/blob/master/src/calibre/devices/smart_device_app/driver.py
 *
 * Uses AsyncTCP for callback-based networking (like KOReader's StreamMessageQueue).
 *
 * Protocol overview:
 * 1. Device broadcasts "hello" on UDP ports 54982, 48123, 39001, 44044, 59678
 * 2. Calibre responds with its TCP server address
 * 3. Device connects to Calibre's TCP server
 * 4. Calibre sends JSON commands with length-prefixed messages
 * 5. Books are transferred as binary data after SEND_BOOK command
 */
class CalibreWirelessActivity final : public Activity {
  // Calibre wireless device states
  enum class WirelessState {
    DISCOVERING,   // Listening for Calibre server broadcasts
    CONNECTING,    // Establishing TCP connection
    WAITING,       // Connected, waiting for commands
    RECEIVING,     // Receiving a book file
    COMPLETE,      // Transfer complete
    DISCONNECTED,  // Calibre disconnected
    ERROR          // Connection/transfer error
  };

  // Calibre protocol opcodes (from calibre/devices/smart_device_app/driver.py)
  enum OpCode : uint8_t {
    OK = 0,
    SET_CALIBRE_DEVICE_INFO = 1,
    SET_CALIBRE_DEVICE_NAME = 2,
    GET_DEVICE_INFORMATION = 3,
    TOTAL_SPACE = 4,
    FREE_SPACE = 5,
    GET_BOOK_COUNT = 6,
    SEND_BOOKLISTS = 7,
    SEND_BOOK = 8,
    GET_INITIALIZATION_INFO = 9,
    BOOK_DONE = 11,
    NOOP = 12,
    DELETE_BOOK = 13,
    GET_BOOK_FILE_SEGMENT = 14,
    GET_BOOK_METADATA = 15,
    SEND_BOOK_METADATA = 16,
    DISPLAY_MESSAGE = 17,
    CALIBRE_BUSY = 18,
    SET_LIBRARY_INFO = 19,
    ERROR = 20,
  };

  TaskHandle_t displayTaskHandle = nullptr;
  TaskHandle_t discoveryTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  SemaphoreHandle_t dataMutex = nullptr;  // Protects shared data accessed from callbacks
  bool updateRequired = false;
  volatile bool shouldExit = false;

  WirelessState state = WirelessState::DISCOVERING;
  const std::function<void()> onCompleteCallback;

  // UDP discovery
  WiFiUDP udp;

  // Async TCP connection
  AsyncClient* tcpClient = nullptr;
  std::string calibreHost;
  uint16_t calibrePort = 0;
  uint16_t calibreAltPort = 0;
  std::string calibreHostname;

  // Transfer state
  std::string currentFilename;
  size_t currentFileSize = 0;
  size_t bytesReceived = 0;
  std::string statusMessage;
  std::string errorMessage;

  // Protocol state
  bool inBinaryMode = false;
  size_t binaryBytesRemaining = 0;
  FsFile currentFile;
  std::string recvBuffer;  // Buffer for incoming data (like KOReader)

  // Large message skip state
  bool inSkipMode = false;
  size_t skipBytesRemaining = 0;
  int skipOpcode = -1;
  std::string skipExtractedLpath;
  size_t skipExtractedLength = 0;

  // Display task
  static void displayTaskTrampoline(void* param);
  void displayTaskLoop();
  void render() const;

  // Discovery task (UDP is not async)
  static void discoveryTaskTrampoline(void* param);
  void discoveryTaskLoop();

  // AsyncTCP callbacks
  void onTcpConnect(AsyncClient* client);
  void onTcpDisconnect(AsyncClient* client);
  void onTcpData(AsyncClient* client, void* data, size_t len);
  void onTcpError(AsyncClient* client, int8_t error);

  // Data processing (called from onTcpData callback)
  void processReceivedData();
  void processBinaryData(const char* data, size_t len);
  void processJsonData();
  bool parseJsonMessage(std::string& message);

  void sendJsonResponse(OpCode opcode, const std::string& data);
  void handleCommand(OpCode opcode, const std::string& data);

  // Protocol handlers
  void handleGetInitializationInfo(const std::string& data);
  void handleGetDeviceInformation();
  void handleFreeSpace();
  void handleGetBookCount();
  void handleSendBook(const std::string& data);
  void handleSendBookMetadata(const std::string& data);
  void handleDisplayMessage(const std::string& data);
  void handleNoop(const std::string& data);

  // Utility
  std::string getDeviceUuid() const;
  void setState(WirelessState newState);
  void setStatus(const std::string& message);
  void setError(const std::string& message);
  void connectToCalibr();

 public:
  explicit CalibreWirelessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onComplete)
      : Activity("CalibreWireless", renderer, mappedInput), onCompleteCallback(onComplete) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }
};
