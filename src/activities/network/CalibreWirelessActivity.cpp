#include "CalibreWirelessActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <WiFi.h>

#include <cstring>

#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;
}  // namespace

void CalibreWirelessActivity::displayTaskTrampoline(void* param) {
  static_cast<CalibreWirelessActivity*>(param)->displayTaskLoop();
}

void CalibreWirelessActivity::discoveryTaskTrampoline(void* param) {
  static_cast<CalibreWirelessActivity*>(param)->discoveryTaskLoop();
}

void CalibreWirelessActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();

  state = WirelessState::DISCOVERING;
  statusMessage = "Discovering Calibre...";
  errorMessage.clear();
  calibreHostname.clear();
  calibreHost.clear();
  calibrePort = 0;
  calibreAltPort = 0;
  currentFilename.clear();
  currentFileSize = 0;
  bytesReceived = 0;
  inBinaryMode = false;
  recvBuffer.clear();
  inSkipMode = false;
  skipBytesRemaining = 0;
  skipOpcode = -1;
  skipExtractedLpath.clear();
  skipExtractedLength = 0;
  shouldExit = false;

  updateRequired = true;

  udp.begin(LOCAL_UDP_PORT);

  // Create display task
  xTaskCreate(&CalibreWirelessActivity::displayTaskTrampoline, "CalDisplayTask", 2048, this, 1, &displayTaskHandle);

  // Create discovery task (UDP is synchronous)
  xTaskCreate(&CalibreWirelessActivity::discoveryTaskTrampoline, "CalDiscoveryTask", 4096, this, 2, &discoveryTaskHandle);
}

void CalibreWirelessActivity::onExit() {
  Activity::onExit();

  shouldExit = true;
  vTaskDelay(50 / portTICK_PERIOD_MS);

  // Close async TCP client
  if (tcpClient) {
    tcpClient->close(true);
    delete tcpClient;
    tcpClient = nullptr;
  }

  udp.stop();
  vTaskDelay(100 / portTICK_PERIOD_MS);

  // Tasks will self-delete when they see shouldExit
  discoveryTaskHandle = nullptr;
  displayTaskHandle = nullptr;

  WiFi.mode(WIFI_OFF);

  if (currentFile) {
    currentFile.close();
  }

  recvBuffer.clear();
  recvBuffer.shrink_to_fit();
  skipExtractedLpath.clear();
  skipExtractedLpath.shrink_to_fit();

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  if (dataMutex) {
    vSemaphoreDelete(dataMutex);
    dataMutex = nullptr;
  }
}

void CalibreWirelessActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCompleteCallback();
    return;
  }
}

void CalibreWirelessActivity::displayTaskLoop() {
  while (!shouldExit) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (!shouldExit) {
        render();
      }
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  vTaskDelete(nullptr);
}

void CalibreWirelessActivity::discoveryTaskLoop() {
  while (!shouldExit && state == WirelessState::DISCOVERING) {
    // Broadcast "hello" on all UDP discovery ports
    for (const uint16_t port : UDP_PORTS) {
      udp.beginPacket("255.255.255.255", port);
      udp.write(reinterpret_cast<const uint8_t*>("hello"), 5);
      udp.endPacket();
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (shouldExit) break;

    const int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[256];
      const int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        std::string response(buffer);

        // Parse Calibre response: "calibre wireless device client (on HOSTNAME);PORT,ALT_PORT"
        size_t onPos = response.find("(on ");
        size_t closePos = response.find(')');
        size_t semiPos = response.find(';');
        size_t commaPos = response.find(',', semiPos);

        if (semiPos != std::string::npos) {
          std::string portStr;
          if (commaPos != std::string::npos && commaPos > semiPos) {
            portStr = response.substr(semiPos + 1, commaPos - semiPos - 1);
            uint16_t altPort = 0;
            for (size_t i = commaPos + 1; i < response.size(); i++) {
              char c = response[i];
              if (c >= '0' && c <= '9') {
                altPort = altPort * 10 + (c - '0');
              } else {
                break;
              }
            }
            calibreAltPort = altPort;
          } else {
            portStr = response.substr(semiPos + 1);
          }

          uint16_t mainPort = 0;
          for (char c : portStr) {
            if (c >= '0' && c <= '9') {
              mainPort = mainPort * 10 + (c - '0');
            } else if (c != ' ' && c != '\t') {
              break;
            }
          }
          calibrePort = mainPort;

          if (onPos != std::string::npos && closePos != std::string::npos && closePos > onPos + 4) {
            calibreHostname = response.substr(onPos + 4, closePos - onPos - 4);
          }
        }

        calibreHost = udp.remoteIP().toString().c_str();
        if (calibreHostname.empty()) {
          calibreHostname = calibreHost;
        }

        if (calibrePort > 0) {
          Serial.printf("[%lu] [CAL] Discovered Calibre at %s:%d (alt:%d)\n", millis(), calibreHost.c_str(), calibrePort,
                        calibreAltPort);
          setState(WirelessState::CONNECTING);
          setStatus("Connecting to " + calibreHostname + "...");
          connectToCalibr();
        }
      }
    }
  }
  vTaskDelete(nullptr);
}

void CalibreWirelessActivity::connectToCalibr() {
  Serial.printf("[%lu] [CAL] connectToCalibr called\n", millis());

  if (tcpClient) {
    tcpClient->close(true);
    delete tcpClient;
    tcpClient = nullptr;
  }

  tcpClient = new AsyncClient();
  if (!tcpClient) {
    Serial.printf("[%lu] [CAL] Failed to create AsyncClient\n", millis());
    setState(WirelessState::DISCOVERING);
    return;
  }

  // Set up callbacks with lambdas that call our member functions
  tcpClient->onConnect(
      [](void* arg, AsyncClient* client) {
        Serial.printf("[%lu] [CAL] onConnect callback fired\n", millis());
        static_cast<CalibreWirelessActivity*>(arg)->onTcpConnect(client);
      },
      this);

  tcpClient->onDisconnect(
      [](void* arg, AsyncClient* client) {
        Serial.printf("[%lu] [CAL] onDisconnect callback fired\n", millis());
        static_cast<CalibreWirelessActivity*>(arg)->onTcpDisconnect(client);
      },
      this);

  tcpClient->onData(
      [](void* arg, AsyncClient* client, void* data, size_t len) {
        static_cast<CalibreWirelessActivity*>(arg)->onTcpData(client, data, len);
      },
      this);

  tcpClient->onError(
      [](void* arg, AsyncClient* client, int8_t error) {
        Serial.printf("[%lu] [CAL] onError callback fired: %d\n", millis(), error);
        static_cast<CalibreWirelessActivity*>(arg)->onTcpError(client, error);
      },
      this);

  // Use IPAddress explicitly to avoid any DNS resolution issues
  IPAddress ip;
  if (!ip.fromString(calibreHost.c_str())) {
    Serial.printf("[%lu] [CAL] Failed to parse IP: %s\n", millis(), calibreHost.c_str());
    setState(WirelessState::DISCOVERING);
    return;
  }

  Serial.printf("[%lu] [CAL] Attempting connect to %s:%d\n", millis(), ip.toString().c_str(), calibrePort);
  bool connectResult = tcpClient->connect(ip, calibrePort);
  Serial.printf("[%lu] [CAL] connect() returned %s\n", millis(), connectResult ? "true" : "false");

  if (!connectResult) {
    // Try alternative port
    if (calibreAltPort > 0) {
      Serial.printf("[%lu] [CAL] Trying alt port %d\n", millis(), calibreAltPort);
      connectResult = tcpClient->connect(ip, calibreAltPort);
      Serial.printf("[%lu] [CAL] alt connect() returned %s\n", millis(), connectResult ? "true" : "false");
      if (!connectResult) {
        setState(WirelessState::DISCOVERING);
        setStatus("Discovering Calibre...\n(Connection failed, retrying)");
      }
    } else {
      setState(WirelessState::DISCOVERING);
      setStatus("Discovering Calibre...\n(Connection failed, retrying)");
    }
  }
  // If connect() returned true, connection is in progress - wait for callbacks
}

void CalibreWirelessActivity::onTcpConnect(AsyncClient* client) {
  Serial.printf("[%lu] [CAL] Connected to Calibre\n", millis());
  setState(WirelessState::WAITING);
  setStatus("Connected to " + calibreHostname + "\nWaiting for commands...");
}

void CalibreWirelessActivity::onTcpDisconnect(AsyncClient* client) {
  Serial.printf("[%lu] [CAL] Disconnected from Calibre\n", millis());
  if (state != WirelessState::ERROR) {
    setState(WirelessState::DISCONNECTED);
    setStatus("Calibre disconnected");
  }
}

void CalibreWirelessActivity::onTcpError(AsyncClient* client, int8_t error) {
  Serial.printf("[%lu] [CAL] TCP error: %d\n", millis(), error);
  setError("Connection error");
}

void CalibreWirelessActivity::onTcpData(AsyncClient* client, void* data, size_t len) {
  // This is the key callback - data arrives here like KOReader's receiveCallback
  const char* charData = static_cast<const char*>(data);

  Serial.printf("[%lu] [CAL] Received %zu bytes\n", millis(), len);

  if (inBinaryMode) {
    processBinaryData(charData, len);
  } else {
    // Append to buffer and process JSON messages
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    recvBuffer.append(charData, len);
    xSemaphoreGive(dataMutex);
    processJsonData();
  }
}

void CalibreWirelessActivity::processBinaryData(const char* data, size_t len) {
  // Like KOReader: write only what we need, put excess in buffer
  size_t toWrite = std::min(len, binaryBytesRemaining);

  if (toWrite > 0) {
    currentFile.write(reinterpret_cast<const uint8_t*>(data), toWrite);
    bytesReceived += toWrite;
    binaryBytesRemaining -= toWrite;
    updateRequired = true;

    // Progress logging
    static unsigned long lastLog = 0;
    unsigned long now = millis();
    if (now - lastLog > 500) {
      Serial.printf("[%lu] [CAL] Binary: %zu/%zu bytes (%.1f%%)\n", now, bytesReceived, currentFileSize,
                    currentFileSize > 0 ? (100.0 * bytesReceived / currentFileSize) : 0.0);
      lastLog = now;
    }
  }

  // If we received more than needed, it's the next JSON message
  if (len > toWrite) {
    size_t excess = len - toWrite;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    recvBuffer.assign(data + toWrite, excess);
    xSemaphoreGive(dataMutex);
    Serial.printf("[%lu] [CAL] Binary complete, %zu excess bytes buffered\n", millis(), excess);
  }

  // Check if binary transfer is complete
  if (binaryBytesRemaining == 0) {
    currentFile.flush();
    currentFile.close();
    inBinaryMode = false;

    Serial.printf("[%lu] [CAL] File complete: %zu bytes\n", millis(), bytesReceived);
    setState(WirelessState::WAITING);
    setStatus("Received: " + currentFilename + "\nWaiting for more...");

    // Process any buffered JSON data
    if (!recvBuffer.empty()) {
      processJsonData();
    }
  }
}

void CalibreWirelessActivity::processJsonData() {
  // Process JSON messages from buffer (like KOReader's onReceiveJSON)
  while (true) {
    std::string message;
    if (!parseJsonMessage(message)) {
      break;  // Need more data
    }

    // Parse opcode from JSON array format: [opcode, {...}]
    size_t start = message.find('[');
    if (start != std::string::npos) {
      start++;
      size_t end = message.find(',', start);
      if (end != std::string::npos) {
        int opcodeInt = 0;
        for (size_t i = start; i < end; i++) {
          char c = message[i];
          if (c >= '0' && c <= '9') {
            opcodeInt = opcodeInt * 10 + (c - '0');
          } else if (c != ' ' && c != '\t') {
            break;
          }
        }

        if (opcodeInt >= 0 && opcodeInt <= OpCode::ERROR) {
          auto opcode = static_cast<OpCode>(opcodeInt);

          // Extract data object
          size_t dataStart = end + 1;
          size_t dataEnd = message.rfind(']');
          std::string data;
          if (dataEnd != std::string::npos && dataEnd > dataStart) {
            data = message.substr(dataStart, dataEnd - dataStart);
          }

          handleCommand(opcode, data);
        }
      }
    }
  }
}

bool CalibreWirelessActivity::parseJsonMessage(std::string& message) {
  constexpr size_t MAX_BUFFERED_MSG_SIZE = 32768;

  xSemaphoreTake(dataMutex, portMAX_DELAY);

  // Handle skip mode for large messages
  if (inSkipMode) {
    if (recvBuffer.size() >= skipBytesRemaining) {
      recvBuffer = recvBuffer.substr(skipBytesRemaining);
      skipBytesRemaining = 0;
      inSkipMode = false;

      if (skipOpcode == OpCode::SEND_BOOK && !skipExtractedLpath.empty() && skipExtractedLength > 0) {
        message = "[" + std::to_string(skipOpcode) + ",{\"lpath\":\"" + skipExtractedLpath +
                  "\",\"length\":" + std::to_string(skipExtractedLength) + "}]";
        skipOpcode = -1;
        skipExtractedLpath.clear();
        skipExtractedLength = 0;
        xSemaphoreGive(dataMutex);
        return true;
      }
      if (skipOpcode >= 0) {
        message = "[" + std::to_string(skipOpcode) + ",{}]";
        skipOpcode = -1;
        xSemaphoreGive(dataMutex);
        return true;
      }
    } else {
      skipBytesRemaining -= recvBuffer.size();
      recvBuffer.clear();
      xSemaphoreGive(dataMutex);
      return false;
    }
  }

  if (recvBuffer.empty()) {
    xSemaphoreGive(dataMutex);
    return false;
  }

  // Find '[' which marks JSON start
  size_t bracketPos = recvBuffer.find('[');
  if (bracketPos == std::string::npos) {
    if (recvBuffer.size() > 1000) {
      recvBuffer.clear();
    }
    xSemaphoreGive(dataMutex);
    return false;
  }

  // Parse length prefix
  size_t msgLen = 0;
  bool validPrefix = false;

  if (bracketPos > 0 && bracketPos <= 12) {
    bool allDigits = true;
    size_t parsedLen = 0;
    for (size_t i = 0; i < bracketPos; i++) {
      char c = recvBuffer[i];
      if (c >= '0' && c <= '9') {
        parsedLen = parsedLen * 10 + (c - '0');
      } else {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      msgLen = parsedLen;
      validPrefix = true;
    }
  }

  if (!validPrefix) {
    if (bracketPos > 0) {
      recvBuffer = recvBuffer.substr(bracketPos);
    }
    xSemaphoreGive(dataMutex);
    return false;
  }

  if (msgLen > 10000000) {
    recvBuffer.clear();
    xSemaphoreGive(dataMutex);
    return false;
  }

  // Handle large messages by extracting essential fields and skipping the rest
  if (msgLen > MAX_BUFFERED_MSG_SIZE) {
    Serial.printf("[%lu] [CAL] Large message (%zu bytes), streaming\n", millis(), msgLen);

    // Extract opcode
    int opcodeInt = -1;
    size_t opcodeStart = bracketPos + 1;
    size_t commaPos = recvBuffer.find(',', opcodeStart);
    if (commaPos != std::string::npos) {
      opcodeInt = 0;
      for (size_t i = opcodeStart; i < commaPos; i++) {
        char c = recvBuffer[i];
        if (c >= '0' && c <= '9') {
          opcodeInt = opcodeInt * 10 + (c - '0');
        } else if (c != ' ' && c != '\t') {
          break;
        }
      }
    }

    skipOpcode = opcodeInt;
    skipExtractedLpath.clear();
    skipExtractedLength = 0;

    // Extract lpath and length for SEND_BOOK
    if (opcodeInt == OpCode::SEND_BOOK) {
      size_t lpathPos = recvBuffer.find("\"lpath\"");
      if (lpathPos != std::string::npos) {
        size_t colonPos = recvBuffer.find(':', lpathPos + 7);
        if (colonPos != std::string::npos) {
          size_t quoteStart = recvBuffer.find('"', colonPos + 1);
          if (quoteStart != std::string::npos) {
            size_t quoteEnd = recvBuffer.find('"', quoteStart + 1);
            if (quoteEnd != std::string::npos) {
              skipExtractedLpath = recvBuffer.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            }
          }
        }
      }

      // Extract top-level length
      int depth = 0;
      const char* lengthKey = "\"length\"";
      for (size_t i = bracketPos; i < recvBuffer.size() && i < bracketPos + 2000; i++) {
        char c = recvBuffer[i];
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (depth == 2 && c == '"' && i + 8 <= recvBuffer.size()) {
          bool match = true;
          for (size_t j = 0; j < 8 && match; j++) {
            if (recvBuffer[i + j] != lengthKey[j]) match = false;
          }
          if (match) {
            size_t numStart = i + 8;
            while (numStart < recvBuffer.size() && (recvBuffer[numStart] == ':' || recvBuffer[numStart] == ' ')) {
              numStart++;
            }
            while (numStart < recvBuffer.size() && recvBuffer[numStart] >= '0' && recvBuffer[numStart] <= '9') {
              skipExtractedLength = skipExtractedLength * 10 + (recvBuffer[numStart] - '0');
              numStart++;
            }
            break;
          }
        }
      }
    }

    size_t totalMsgBytes = bracketPos + msgLen;
    if (recvBuffer.size() >= totalMsgBytes) {
      recvBuffer = recvBuffer.substr(totalMsgBytes);
      if (skipOpcode == OpCode::SEND_BOOK && !skipExtractedLpath.empty() && skipExtractedLength > 0) {
        message = "[" + std::to_string(skipOpcode) + ",{\"lpath\":\"" + skipExtractedLpath +
                  "\",\"length\":" + std::to_string(skipExtractedLength) + "}]";
        skipOpcode = -1;
        skipExtractedLpath.clear();
        skipExtractedLength = 0;
        xSemaphoreGive(dataMutex);
        return true;
      }
      if (skipOpcode >= 0) {
        message = "[" + std::to_string(skipOpcode) + ",{}]";
        skipOpcode = -1;
        xSemaphoreGive(dataMutex);
        return true;
      }
    } else {
      skipBytesRemaining = totalMsgBytes - recvBuffer.size();
      recvBuffer.clear();
      inSkipMode = true;
    }
    xSemaphoreGive(dataMutex);
    return false;
  }

  // Normal message handling
  size_t totalNeeded = bracketPos + msgLen;
  if (recvBuffer.size() < totalNeeded) {
    xSemaphoreGive(dataMutex);
    return false;
  }

  message = recvBuffer.substr(bracketPos, msgLen);
  recvBuffer = recvBuffer.size() > totalNeeded ? recvBuffer.substr(totalNeeded) : "";

  xSemaphoreGive(dataMutex);
  return true;
}

void CalibreWirelessActivity::sendJsonResponse(const OpCode opcode, const std::string& data) {
  if (!tcpClient || !tcpClient->connected()) return;

  std::string json = "[" + std::to_string(opcode) + "," + data + "]";
  std::string msg = std::to_string(json.length()) + json;

  tcpClient->write(msg.c_str(), msg.length());
}

void CalibreWirelessActivity::handleCommand(const OpCode opcode, const std::string& data) {
  Serial.printf("[%lu] [CAL] Command: %d, data size: %zu\n", millis(), opcode, data.size());

  switch (opcode) {
    case OpCode::GET_INITIALIZATION_INFO:
      handleGetInitializationInfo(data);
      break;
    case OpCode::GET_DEVICE_INFORMATION:
      handleGetDeviceInformation();
      break;
    case OpCode::FREE_SPACE:
    case OpCode::TOTAL_SPACE:
      handleFreeSpace();
      break;
    case OpCode::GET_BOOK_COUNT:
      handleGetBookCount();
      break;
    case OpCode::SEND_BOOK:
      handleSendBook(data);
      break;
    case OpCode::SEND_BOOK_METADATA:
      handleSendBookMetadata(data);
      break;
    case OpCode::DISPLAY_MESSAGE:
      handleDisplayMessage(data);
      break;
    case OpCode::NOOP:
      handleNoop(data);
      break;
    case OpCode::SET_CALIBRE_DEVICE_INFO:
    case OpCode::SET_CALIBRE_DEVICE_NAME:
    case OpCode::SET_LIBRARY_INFO:
    case OpCode::SEND_BOOKLISTS:
      sendJsonResponse(OpCode::OK, "{}");
      break;
    default:
      Serial.printf("[%lu] [CAL] Unknown opcode: %d\n", millis(), opcode);
      sendJsonResponse(OpCode::OK, "{}");
      break;
  }
}

void CalibreWirelessActivity::handleGetInitializationInfo(const std::string& data) {
  setState(WirelessState::WAITING);
  setStatus("Connected to " + calibreHostname +
            "\nWaiting for transfer...\n\nIf transfer fails, enable\n'Ignore free space' in Calibre's\nSmartDevice "
            "plugin settings.");

  std::string response = "{";
  response += "\"appName\":\"CrossPoint\",";
  response += "\"acceptedExtensions\":[\"epub\"],";
  response += "\"cacheUsesLpaths\":true,";
  response += "\"canAcceptLibraryInfo\":true,";
  response += "\"canDeleteMultipleBooks\":true,";
  response += "\"canReceiveBookBinary\":true,";
  response += "\"canSendOkToSendbook\":true,";
  response += "\"canStreamBooks\":true,";
  response += "\"canStreamMetadata\":true,";
  response += "\"canUseCachedMetadata\":true,";
  response += "\"ccVersionNumber\":212,";
  response += "\"coverHeight\":0,";
  response += "\"deviceKind\":\"CrossPoint\",";
  response += "\"deviceName\":\"CrossPoint\",";
  response += "\"extensionPathLengths\":{\"epub\":37},";
  response += "\"maxBookContentPacketLen\":4096,";
  response += "\"passwordHash\":\"\",";
  response += "\"useUuidFileNames\":false,";
  response += "\"versionOK\":true";
  response += "}";

  sendJsonResponse(OpCode::OK, response);
}

void CalibreWirelessActivity::handleGetDeviceInformation() {
  std::string response = "{";
  response += "\"device_info\":{";
  response += "\"device_store_uuid\":\"" + getDeviceUuid() + "\",";
  response += "\"device_name\":\"CrossPoint Reader\",";
  response += "\"device_version\":\"" CROSSPOINT_VERSION "\"";
  response += "},";
  response += "\"version\":1,";
  response += "\"device_version\":\"" CROSSPOINT_VERSION "\"";
  response += "}";

  sendJsonResponse(OpCode::OK, response);
}

void CalibreWirelessActivity::handleFreeSpace() {
  sendJsonResponse(OpCode::OK, "{\"free_space_on_device\":10737418240}");
}

void CalibreWirelessActivity::handleGetBookCount() {
  sendJsonResponse(OpCode::OK, "{\"count\":0,\"willStream\":true,\"willScan\":false}");
}

void CalibreWirelessActivity::handleSendBook(const std::string& data) {
  Serial.printf("[%lu] [CAL] SEND_BOOK data (first 500 chars): %.500s\n", millis(), data.c_str());

  // Extract lpath
  std::string lpath;
  size_t lpathPos = data.find("\"lpath\"");
  if (lpathPos != std::string::npos) {
    size_t colonPos = data.find(':', lpathPos + 7);
    if (colonPos != std::string::npos) {
      size_t quoteStart = data.find('"', colonPos + 1);
      if (quoteStart != std::string::npos) {
        size_t quoteEnd = data.find('"', quoteStart + 1);
        if (quoteEnd != std::string::npos) {
          lpath = data.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
      }
    }
  }

  // Extract top-level length
  size_t length = 0;
  int depth = 0;
  const char* lengthKey = "\"length\"";

  for (size_t i = 0; i < data.size(); i++) {
    char c = data[i];
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') depth--;
    else if (depth == 1 && c == '"' && i + 8 <= data.size()) {
      bool match = true;
      for (size_t j = 0; j < 8 && match; j++) {
        if (data[i + j] != lengthKey[j]) match = false;
      }
      if (match) {
        size_t colonPos = i + 8;
        while (colonPos < data.size() && data[colonPos] != ':') colonPos++;
        if (colonPos < data.size()) {
          size_t numStart = colonPos + 1;
          while (numStart < data.size() && (data[numStart] == ' ' || data[numStart] == '\t')) numStart++;
          while (numStart < data.size() && data[numStart] >= '0' && data[numStart] <= '9') {
            length = length * 10 + (data[numStart] - '0');
            numStart++;
          }
          if (length > 0) {
            Serial.printf("[%lu] [CAL] Extracted length=%zu\n", millis(), length);
            break;
          }
        }
      }
    }
  }

  if (lpath.empty() || length == 0) {
    sendJsonResponse(OpCode::ERROR, "{\"message\":\"Invalid book data\"}");
    return;
  }

  std::string filename = lpath;
  size_t lastSlash = filename.rfind('/');
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  currentFilename = "/" + StringUtils::sanitizeFilename(filename);
  if (!StringUtils::checkFileExtension(currentFilename, ".epub")) {
    currentFilename += ".epub";
  }
  currentFileSize = length;
  bytesReceived = 0;
  binaryBytesRemaining = length;

  Serial.printf("[%lu] [CAL] SEND_BOOK: file='%s', length=%zu\n", millis(), currentFilename.c_str(), length);

  setState(WirelessState::RECEIVING);
  setStatus("Receiving: " + filename);

  if (!SdMan.openFileForWrite("CAL", currentFilename.c_str(), currentFile)) {
    setError("Failed to create file");
    sendJsonResponse(OpCode::ERROR, "{\"message\":\"Failed to create file\"}");
    return;
  }

  // Send OK - Calibre will start sending binary data
  sendJsonResponse(OpCode::OK, "{}");

  // Switch to binary mode - subsequent data in onTcpData will be file content
  inBinaryMode = true;

  // Process any data already in buffer (like KOReader)
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (!recvBuffer.empty()) {
    size_t toWrite = std::min(recvBuffer.size(), binaryBytesRemaining);
    Serial.printf("[%lu] [CAL] Writing %zu bytes from buffer\n", millis(), toWrite);
    currentFile.write(reinterpret_cast<const uint8_t*>(recvBuffer.data()), toWrite);
    bytesReceived += toWrite;
    binaryBytesRemaining -= toWrite;

    if (recvBuffer.size() > toWrite) {
      recvBuffer = recvBuffer.substr(toWrite);
    } else {
      recvBuffer.clear();
    }
    updateRequired = true;

    if (binaryBytesRemaining == 0) {
      currentFile.flush();
      currentFile.close();
      inBinaryMode = false;
      Serial.printf("[%lu] [CAL] File complete: %zu bytes\n", millis(), bytesReceived);
      setState(WirelessState::WAITING);
      setStatus("Received: " + currentFilename + "\nWaiting for more...");
    }
  }
  xSemaphoreGive(dataMutex);
}

void CalibreWirelessActivity::handleSendBookMetadata(const std::string& data) {
  Serial.printf("[%lu] [CAL] SEND_BOOK_METADATA received\n", millis());
  sendJsonResponse(OpCode::OK, "{}");
}

void CalibreWirelessActivity::handleDisplayMessage(const std::string& data) {
  if (data.find("\"messageKind\":1") != std::string::npos) {
    setError("Password required");
  }
  sendJsonResponse(OpCode::OK, "{}");
}

void CalibreWirelessActivity::handleNoop(const std::string& data) {
  if (data.find("\"ejecting\":true") != std::string::npos) {
    setState(WirelessState::DISCONNECTED);
    setStatus("Calibre disconnected");
  }
  sendJsonResponse(OpCode::NOOP, "{}");
}

void CalibreWirelessActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 30, "Calibre Wireless", true, EpdFontFamily::BOLD);

  const std::string ipAddr = WiFi.localIP().toString().c_str();
  renderer.drawCenteredText(UI_10_FONT_ID, 60, ("IP: " + ipAddr).c_str());

  int statusY = pageHeight / 2 - 40;
  std::string status = statusMessage;
  size_t pos = 0;
  while ((pos = status.find('\n')) != std::string::npos) {
    renderer.drawCenteredText(UI_10_FONT_ID, statusY, status.substr(0, pos).c_str());
    statusY += 25;
    status = status.substr(pos + 1);
  }
  if (!status.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, statusY, status.c_str());
    statusY += 25;
  }

  if (state == WirelessState::RECEIVING && currentFileSize > 0) {
    const int barWidth = pageWidth - 100;
    constexpr int barHeight = 20;
    constexpr int barX = 50;
    const int barY = statusY + 20;
    ScreenComponents::drawProgressBar(renderer, barX, barY, barWidth, barHeight, bytesReceived, currentFileSize);
  }

  if (!errorMessage.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, errorMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

std::string CalibreWirelessActivity::getDeviceUuid() const {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char uuid[37];
  snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-4000-8000-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(uuid);
}

void CalibreWirelessActivity::setState(WirelessState newState) {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  state = newState;
  xSemaphoreGive(dataMutex);
  updateRequired = true;
}

void CalibreWirelessActivity::setStatus(const std::string& message) {
  statusMessage = message;
  updateRequired = true;
}

void CalibreWirelessActivity::setError(const std::string& message) {
  errorMessage = message;
  setState(WirelessState::ERROR);
}
