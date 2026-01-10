#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_pm.h>

#include <algorithm>

#include "html/FilesPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);

// CPU frequency for upload boost (240MHz for maximum performance)
constexpr uint32_t UPLOAD_CPU_FREQ_MHZ = 240;
constexpr uint32_t NORMAL_CPU_FREQ_MHZ = 160;

// Speed calculation interval
constexpr unsigned long SPEED_CALC_INTERVAL_MS = 500;
}  // namespace

CrossPointWebServer::CrossPointWebServer() {
  uploadMutex = xSemaphoreCreateMutex();
  uploadPath = "/";
}

CrossPointWebServer::~CrossPointWebServer() {
  stop();
  freeUploadBuffer();
  if (uploadMutex) {
    vSemaphoreDelete(uploadMutex);
    uploadMutex = nullptr;
  }
}

// Buffer management functions
bool CrossPointWebServer::allocateUploadBuffer() const {
  if (uploadBuffer) return true;  // Already allocated

  uploadBuffer = static_cast<uint8_t*>(malloc(UPLOAD_BUFFER_SIZE));
  if (!uploadBuffer) {
    Serial.printf("[%lu] [WEB] [UPLOAD] ERROR: Failed to allocate %d byte upload buffer!\n", millis(),
                  UPLOAD_BUFFER_SIZE);
    return false;
  }

  uploadBufferHead = 0;
  uploadBufferTail = 0;
  Serial.printf("[%lu] [WEB] [UPLOAD] Allocated %dKB upload buffer, free heap: %d\n", millis(),
                UPLOAD_BUFFER_SIZE / 1024, ESP.getFreeHeap());
  return true;
}

void CrossPointWebServer::freeUploadBuffer() const {
  if (uploadBuffer) {
    free(uploadBuffer);
    uploadBuffer = nullptr;
    uploadBufferHead = 0;
    uploadBufferTail = 0;
    Serial.printf("[%lu] [WEB] [UPLOAD] Freed upload buffer, free heap: %d\n", millis(), ESP.getFreeHeap());
  }
}

size_t CrossPointWebServer::bufferUsed() const {
  if (uploadBufferHead >= uploadBufferTail) {
    return uploadBufferHead - uploadBufferTail;
  }
  return UPLOAD_BUFFER_SIZE - uploadBufferTail + uploadBufferHead;
}

size_t CrossPointWebServer::bufferFree() const { return UPLOAD_BUFFER_SIZE - bufferUsed() - 1; }

bool CrossPointWebServer::writeToBuffer(const uint8_t* data, size_t len) const {
  if (!uploadBuffer || len > bufferFree()) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    uploadBuffer[uploadBufferHead] = data[i];
    uploadBufferHead = (uploadBufferHead + 1) % UPLOAD_BUFFER_SIZE;
  }
  return true;
}

size_t CrossPointWebServer::flushBufferToSD(size_t maxBytes) const {
  if (!uploadBuffer || !uploadFile) return 0;

  const size_t available = bufferUsed();
  if (available == 0) return 0;

  size_t toWrite = maxBytes > 0 ? std::min(available, maxBytes) : available;
  size_t totalWritten = 0;

  // Write in chunks to avoid blocking too long
  constexpr size_t CHUNK_SIZE = 4096;
  uint8_t chunk[CHUNK_SIZE];

  while (toWrite > 0) {
    const size_t chunkLen = std::min(toWrite, CHUNK_SIZE);

    // Copy from circular buffer to linear chunk
    for (size_t i = 0; i < chunkLen; i++) {
      chunk[i] = uploadBuffer[uploadBufferTail];
      uploadBufferTail = (uploadBufferTail + 1) % UPLOAD_BUFFER_SIZE;
    }

    const size_t written = uploadFile.write(chunk, chunkLen);
    totalWritten += written;

    if (written != chunkLen) {
      Serial.printf("[%lu] [WEB] [UPLOAD] SD write error: expected %d, wrote %d\n", millis(), chunkLen, written);
      break;
    }

    toWrite -= chunkLen;
    yield();  // Allow WiFi stack to process
  }

  return totalWritten;
}

// CPU frequency management
void CrossPointWebServer::boostCPU() const {
  if (cpuBoosted) return;

  if (setCpuFrequencyMhz(UPLOAD_CPU_FREQ_MHZ)) {
    cpuBoosted = true;
    Serial.printf("[%lu] [WEB] [UPLOAD] CPU boosted to %dMHz\n", millis(), UPLOAD_CPU_FREQ_MHZ);
  }
}

void CrossPointWebServer::restoreCPU() const {
  if (!cpuBoosted) return;

  if (setCpuFrequencyMhz(NORMAL_CPU_FREQ_MHZ)) {
    cpuBoosted = false;
    Serial.printf("[%lu] [WEB] [UPLOAD] CPU restored to %dMHz\n", millis(), NORMAL_CPU_FREQ_MHZ);
  }
}

// Thread-safe upload status getters
bool CrossPointWebServer::isUploading() const {
  if (!uploadMutex) return false;
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  const bool result = uploadInProgress;
  xSemaphoreGive(uploadMutex);
  return result;
}

String CrossPointWebServer::getCurrentUploadFile() const {
  if (!uploadMutex) return "";
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  String result = uploadFileName;
  xSemaphoreGive(uploadMutex);
  return result;
}

float CrossPointWebServer::getCurrentUploadSpeed() const {
  if (!uploadMutex) return 0.0f;
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  const float result = uploadSpeedKBps;
  xSemaphoreGive(uploadMutex);
  return result;
}

uint8_t CrossPointWebServer::getUploadProgress() const {
  if (!uploadMutex) return 0;
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  uint8_t result = 0;
  if (uploadTotalExpected > 0) {
    result = static_cast<uint8_t>((uploadSize * 100) / uploadTotalExpected);
  }
  xSemaphoreGive(uploadMutex);
  return result;
}

void CrossPointWebServer::begin() {
  if (running) {
    Serial.printf("[%lu] [WEB] Web server already running\n", millis());
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    Serial.printf("[%lu] [WEB] Cannot start webserver - no valid network (mode=%d, status=%d)\n", millis(), wifiMode,
                  WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  Serial.printf("[%lu] [WEB] [MEM] Free heap before begin: %d bytes\n", millis(), ESP.getFreeHeap());
  Serial.printf("[%lu] [WEB] Network mode: %s\n", millis(), apMode ? "AP" : "STA");

  Serial.printf("[%lu] [WEB] Creating web server on port %d...\n", millis(), port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  Serial.printf("[%lu] [WEB] [MEM] Free heap after WebServer allocation: %d bytes\n", millis(), ESP.getFreeHeap());

  if (!server) {
    Serial.printf("[%lu] [WEB] Failed to create WebServer!\n", millis());
    return;
  }

  // Setup routes
  Serial.printf("[%lu] [WEB] Setting up routes...\n", millis());
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  server->onNotFound([this] { handleNotFound(); });
  Serial.printf("[%lu] [WEB] [MEM] Free heap after route setup: %d bytes\n", millis(), ESP.getFreeHeap());

  server->begin();
  running = true;

  Serial.printf("[%lu] [WEB] Web server started on port %d\n", millis(), port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  Serial.printf("[%lu] [WEB] Access at http://%s/\n", millis(), ipAddr.c_str());
  Serial.printf("[%lu] [WEB] [MEM] Free heap after server.begin(): %d bytes\n", millis(), ESP.getFreeHeap());
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    Serial.printf("[%lu] [WEB] stop() called but already stopped (running=%d, server=%p)\n", millis(), running,
                  server.get());
    return;
  }

  Serial.printf("[%lu] [WEB] STOP INITIATED - setting running=false first\n", millis());
  running = false;  // Set this FIRST to prevent handleClient from using server

  Serial.printf("[%lu] [WEB] [MEM] Free heap before stop: %d bytes\n", millis(), ESP.getFreeHeap());

  // Add delay to allow any in-flight handleClient() calls to complete
  delay(100);
  Serial.printf("[%lu] [WEB] Waited 100ms for handleClient to finish\n", millis());

  server->stop();
  Serial.printf("[%lu] [WEB] [MEM] Free heap after server->stop(): %d bytes\n", millis(), ESP.getFreeHeap());

  // Add another delay before deletion to ensure server->stop() completes
  delay(50);
  Serial.printf("[%lu] [WEB] Waited 50ms before deleting server\n", millis());

  server.reset();
  Serial.printf("[%lu] [WEB] Web server stopped and deleted\n", millis());
  Serial.printf("[%lu] [WEB] [MEM] Free heap after delete server: %d bytes\n", millis(), ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  Serial.printf("[%lu] [WEB] [MEM] Free heap final: %d bytes\n", millis(), ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() const {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    Serial.printf("[%lu] [WEB] WARNING: handleClient called with null server!\n", millis());
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    Serial.printf("[%lu] [WEB] handleClient active, server running on port %d\n", millis(), port);
    lastDebugPrint = millis();
  }

  server->handleClient();
}

void CrossPointWebServer::handleRoot() const {
  server->send(200, "text/html", HomePageHtml);
  Serial.printf("[%lu] [WEB] Served root page\n", millis());
}

void CrossPointWebServer::handleNotFound() const {
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  FsFile root = SdMan.open(path);
  if (!root) {
    Serial.printf("[%lu] [WEB] Failed to open directory: %s\n", millis(), path);
    return;
  }

  if (!root.isDirectory()) {
    Serial.printf("[%lu] [WEB] Not a directory: %s\n", millis(), path);
    root.close();
    return;
  }

  Serial.printf("[%lu] [WEB] Scanning files in: %s\n", millis(), path);

  FsFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
        if (fileName.equals(HIDDEN_ITEMS[i])) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();  // Yield to allow WiFi and other tasks to process during long scans
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const {
  String lower = filename;
  lower.toLowerCase();
  return lower.endsWith(".epub");
}

void CrossPointWebServer::handleFileList() const { server->send(200, "text/html", FilesPageHtml); }

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = server->arg("path");
    // Ensure path starts with /
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    // Remove trailing slash unless it's root
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      Serial.printf("[%lu] [WEB] Skipping file entry with oversized JSON for name: %s\n", millis(), info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  Serial.printf("[%lu] [WEB] Served file listing page for path: %s\n", millis(), currentPath.c_str());
}

void CrossPointWebServer::handleUpload() const {
  // Safety check: ensure server is still valid
  if (!running || !server) {
    Serial.printf("[%lu] [WEB] [UPLOAD] ERROR: handleUpload called but server not running!\n", millis());
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    xSemaphoreTake(uploadMutex, portMAX_DELAY);

    uploadFileName = upload.filename;
    uploadSize = 0;
    uploadTotalExpected = upload.totalSize;  // May be 0 if unknown
    uploadSuccess = false;
    uploadError = "";
    uploadStartTime = millis();
    lastSpeedCalcTime = millis();
    lastSpeedCalcSize = 0;
    uploadSpeedKBps = 0.0f;
    uploadInProgress = true;

    // Get upload path from query parameter
    if (server->hasArg("path")) {
      uploadPath = server->arg("path");
      if (!uploadPath.startsWith("/")) {
        uploadPath = "/" + uploadPath;
      }
      if (uploadPath.length() > 1 && uploadPath.endsWith("/")) {
        uploadPath = uploadPath.substring(0, uploadPath.length() - 1);
      }
    } else {
      uploadPath = "/";
    }

    xSemaphoreGive(uploadMutex);

    Serial.printf("[%lu] [WEB] [UPLOAD] START: %s to path: %s\n", millis(), uploadFileName.c_str(), uploadPath.c_str());
    Serial.printf("[%lu] [WEB] [UPLOAD] Free heap: %d bytes\n", millis(), ESP.getFreeHeap());

    // Allocate upload buffer and boost CPU
    if (!allocateUploadBuffer()) {
      xSemaphoreTake(uploadMutex, portMAX_DELAY);
      uploadError = "Failed to allocate upload buffer";
      uploadInProgress = false;
      xSemaphoreGive(uploadMutex);
      return;
    }

    boostCPU();

    // Create file path
    String filePath = uploadPath;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += uploadFileName;

    // Check if file already exists
    if (SdMan.exists(filePath.c_str())) {
      Serial.printf("[%lu] [WEB] [UPLOAD] Overwriting existing file: %s\n", millis(), filePath.c_str());
      SdMan.remove(filePath.c_str());
    }

    // Open file for writing
    if (!SdMan.openFileForWrite("WEB", filePath, uploadFile)) {
      xSemaphoreTake(uploadMutex, portMAX_DELAY);
      uploadError = "Failed to create file on SD card";
      uploadInProgress = false;
      xSemaphoreGive(uploadMutex);
      restoreCPU();
      freeUploadBuffer();
      Serial.printf("[%lu] [WEB] [UPLOAD] FAILED to create file: %s\n", millis(), filePath.c_str());
      return;
    }

    Serial.printf("[%lu] [WEB] [UPLOAD] File created successfully: %s\n", millis(), filePath.c_str());

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    xSemaphoreTake(uploadMutex, portMAX_DELAY);
    const bool hasError = !uploadError.isEmpty();
    xSemaphoreGive(uploadMutex);

    if (uploadFile && !hasError) {
      // Try to write to buffer first (fast path - doesn't block on SD)
      if (!writeToBuffer(upload.buf, upload.currentSize)) {
        // Buffer full - need to flush to SD first
        const size_t flushed = flushBufferToSD(UPLOAD_BATCH_WRITE_SIZE);
        if (flushed == 0) {
          // Direct write as fallback
          const size_t written = uploadFile.write(upload.buf, upload.currentSize);
          if (written != upload.currentSize) {
            xSemaphoreTake(uploadMutex, portMAX_DELAY);
            uploadError = "Failed to write to SD card - disk may be full";
            xSemaphoreGive(uploadMutex);
            uploadFile.close();
            Serial.printf("[%lu] [WEB] [UPLOAD] WRITE ERROR - expected %d, wrote %d\n", millis(), upload.currentSize,
                          written);
            return;
          }
        } else {
          // Try buffer again after flush
          if (!writeToBuffer(upload.buf, upload.currentSize)) {
            // Still can't fit - direct write
            const size_t written = uploadFile.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
              xSemaphoreTake(uploadMutex, portMAX_DELAY);
              uploadError = "Failed to write to SD card - disk may be full";
              xSemaphoreGive(uploadMutex);
              uploadFile.close();
              return;
            }
          }
        }
      }

      // Flush buffer when it reaches threshold
      if (bufferUsed() >= UPLOAD_BATCH_WRITE_SIZE) {
        flushBufferToSD(UPLOAD_BATCH_WRITE_SIZE);
      }

      xSemaphoreTake(uploadMutex, portMAX_DELAY);
      uploadSize += upload.currentSize;

      // Calculate speed every 500ms
      const unsigned long now = millis();
      if (now - lastSpeedCalcTime >= SPEED_CALC_INTERVAL_MS) {
        const size_t bytesSinceLastCalc = uploadSize - lastSpeedCalcSize;
        const float secondsElapsed = (now - lastSpeedCalcTime) / 1000.0f;
        if (secondsElapsed > 0) {
          uploadSpeedKBps = (bytesSinceLastCalc / 1024.0f) / secondsElapsed;
        }
        lastSpeedCalcTime = now;
        lastSpeedCalcSize = uploadSize;

        // Log progress
        const float avgSpeed = (uploadSize / 1024.0f) / ((now - uploadStartTime) / 1000.0f);
        Serial.printf("[%lu] [WEB] [UPLOAD] Progress: %d bytes (%.1f KB), current: %.1f KB/s, avg: %.1f KB/s\n",
                      millis(), uploadSize, uploadSize / 1024.0f, uploadSpeedKBps, avgSpeed);
      }
      xSemaphoreGive(uploadMutex);
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      // Flush remaining buffer to SD
      flushBufferToSD();
      uploadFile.close();

      xSemaphoreTake(uploadMutex, portMAX_DELAY);
      if (uploadError.isEmpty()) {
        uploadSuccess = true;
        const unsigned long duration = millis() - uploadStartTime;
        const float avgSpeed = (uploadSize / 1024.0f) / (duration / 1000.0f);
        Serial.printf("[%lu] [WEB] [UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)\n", millis(),
                      uploadFileName.c_str(), uploadSize, duration, avgSpeed);
      }
      uploadInProgress = false;
      xSemaphoreGive(uploadMutex);
    }

    restoreCPU();
    freeUploadBuffer();

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
      String filePath = uploadPath;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += uploadFileName;
      SdMan.remove(filePath.c_str());
    }

    xSemaphoreTake(uploadMutex, portMAX_DELAY);
    uploadError = "Upload aborted";
    uploadInProgress = false;
    xSemaphoreGive(uploadMutex);

    restoreCPU();
    freeUploadBuffer();
    Serial.printf("[%lu] [WEB] [UPLOAD] Aborted\n", millis());
  }
}

void CrossPointWebServer::handleUploadPost() const {
  if (uploadSuccess) {
    server->send(200, "text/plain", "File uploaded successfully: " + uploadFileName);
  } else {
    const String error = uploadError.isEmpty() ? "Unknown error during upload" : uploadError;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = server->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  Serial.printf("[%lu] [WEB] Creating folder: %s\n", millis(), folderPath.c_str());

  // Check if already exists
  if (SdMan.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (SdMan.mkdir(folderPath.c_str())) {
    Serial.printf("[%lu] [WEB] Folder created successfully: %s\n", millis(), folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    Serial.printf("[%lu] [WEB] Failed to create folder: %s\n", millis(), folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleDelete() const {
  // Get path from form data
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  const String itemType = server->hasArg("type") ? server->arg("type") : "file";

  // Validate path
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot delete root directory");
    return;
  }

  // Ensure path starts with /
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  // Security check: prevent deletion of protected items
  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  // Check if item starts with a dot (hidden/system file)
  if (itemName.startsWith(".")) {
    Serial.printf("[%lu] [WEB] Delete rejected - hidden/system item: %s\n", millis(), itemPath.c_str());
    server->send(403, "text/plain", "Cannot delete system files");
    return;
  }

  // Check against explicitly protected items
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      Serial.printf("[%lu] [WEB] Delete rejected - protected item: %s\n", millis(), itemPath.c_str());
      server->send(403, "text/plain", "Cannot delete protected items");
      return;
    }
  }

  // Check if item exists
  if (!SdMan.exists(itemPath.c_str())) {
    Serial.printf("[%lu] [WEB] Delete failed - item not found: %s\n", millis(), itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  Serial.printf("[%lu] [WEB] Attempting to delete %s: %s\n", millis(), itemType.c_str(), itemPath.c_str());

  bool success = false;

  if (itemType == "folder") {
    // For folders, try to remove (will fail if not empty)
    FsFile dir = SdMan.open(itemPath.c_str());
    if (dir && dir.isDirectory()) {
      // Check if folder is empty
      FsFile entry = dir.openNextFile();
      if (entry) {
        // Folder is not empty
        entry.close();
        dir.close();
        Serial.printf("[%lu] [WEB] Delete failed - folder not empty: %s\n", millis(), itemPath.c_str());
        server->send(400, "text/plain", "Folder is not empty. Delete contents first.");
        return;
      }
      dir.close();
    }
    success = SdMan.rmdir(itemPath.c_str());
  } else {
    // For files, use remove
    success = SdMan.remove(itemPath.c_str());
  }

  if (success) {
    Serial.printf("[%lu] [WEB] Successfully deleted: %s\n", millis(), itemPath.c_str());
    server->send(200, "text/plain", "Deleted successfully");
  } else {
    Serial.printf("[%lu] [WEB] Failed to delete: %s\n", millis(), itemPath.c_str());
    server->send(500, "text/plain", "Failed to delete item");
  }
}
