#include "OpdsBookBrowserActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr int SKIP_PAGE_MS = 700;
constexpr char OPDS_ROOT_PATH[] = "opds";  // No leading slash - relative to server URL

// Prepend http:// if no protocol specified (server will redirect to https if needed)
std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

// Extract host with protocol from URL (e.g., "http://example.com" from "http://example.com/path")
std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    // No protocol, find first slash
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  // Find the first slash after the protocol
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

// Build full URL from server URL and path
// If path starts with /, it's an absolute path from the host root
// Otherwise, it's relative to the server URL
std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return urlWithProtocol;
  }
  if (path[0] == '/') {
    // Absolute path - use just the host
    return extractHost(urlWithProtocol) + path;
  }
  // Relative path - append to server URL
  if (urlWithProtocol.back() == '/') {
    return urlWithProtocol + path;
  }
  return urlWithProtocol + "/" + path;
}
}  // namespace

void OpdsBookBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(param);
  self->displayTaskLoop();
}

void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = BrowserState::LOADING;
  entries.clear();
  navigationHistory.clear();
  currentPath = OPDS_ROOT_PATH;
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = "Loading...";
  updateRequired = true;

  xTaskCreate(&OpdsBookBrowserActivity::taskTrampoline, "OpdsBookBrowserTask",
              4096,               // Stack size (larger for HTTP operations)
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // Fetch feed after setting up the display task
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  entries.clear();
  navigationHistory.clear();
}

void OpdsBookBrowserActivity::loop() {
  // Handle error state - Confirm retries, Back goes back or home
  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = BrowserState::LOADING;
      statusMessage = "Loading...";
      updateRequired = true;
      fetchFeed(currentPath);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  // Handle loading state - only Back works
  if (state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  // Handle downloading state - no input allowed
  if (state == BrowserState::DOWNLOADING) {
    return;
  }

  // Handle browsing state
  if (state == BrowserState::BROWSING) {
    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!entries.empty()) {
        const auto& entry = entries[selectorIndex];
        if (entry.type == OpdsEntryType::BOOK) {
          downloadBook(entry);
        } else {
          navigateToEntry(entry);
        }
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (prevReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS - 1) * PAGE_ITEMS + entries.size()) % entries.size();
      } else {
        selectorIndex = (selectorIndex + entries.size() - 1) % entries.size();
      }
      updateRequired = true;
    } else if (nextReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS + 1) * PAGE_ITEMS) % entries.size();
      } else {
        selectorIndex = (selectorIndex + 1) % entries.size();
      }
      updateRequired = true;
    }
  }
}

void OpdsBookBrowserActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OpdsBookBrowserActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Calibre Library", true, EpdFontFamily::BOLD);

  if (state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Error:");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels("« Back", "Retry", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Downloading...");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, statusMessage.c_str());
    if (downloadTotal > 0) {
      const int percent = (downloadProgress * 100) / downloadTotal;
      char progressText[32];
      snprintf(progressText, sizeof(progressText), "%d%%", percent);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, progressText);
    }
    renderer.displayBuffer();
    return;
  }

  // Browsing state
  // Show appropriate button hint based on selected entry type
  const char* confirmLabel = "Open";
  if (!entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK) {
    confirmLabel = "Download";
  }
  const auto labels = mappedInput.mapLabels("« Back", confirmLabel, "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No entries found");
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
  renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

  for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
    const auto& entry = entries[i];

    // Format display text with type indicator
    std::string displayText;
    if (entry.type == OpdsEntryType::NAVIGATION) {
      displayText = "> " + entry.title;  // Folder/navigation indicator
    } else {
      // Book: "Title - Author" or just "Title"
      displayText = entry.title;
      if (!entry.author.empty()) {
        displayText += " - " + entry.author;
      }
    }

    auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), renderer.getScreenWidth() - 40);
    renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                      i != static_cast<size_t>(selectorIndex));
  }

  renderer.displayBuffer();
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  const char* serverUrl = SETTINGS.opdsServerUrl;
  if (strlen(serverUrl) == 0) {
    state = BrowserState::ERROR;
    errorMessage = "No server URL configured";
    updateRequired = true;
    return;
  }

  std::string url = buildUrl(serverUrl, path);
  Serial.printf("[%lu] [OPDS] Fetching: %s\n", millis(), url.c_str());

  std::string content;
  if (!HttpDownloader::fetchUrl(url, content)) {
    state = BrowserState::ERROR;
    errorMessage = "Failed to fetch feed";
    updateRequired = true;
    return;
  }

  OpdsParser parser;
  if (!parser.parse(content.c_str(), content.size())) {
    state = BrowserState::ERROR;
    errorMessage = "Failed to parse feed";
    updateRequired = true;
    return;
  }

  entries = parser.getEntries();
  selectorIndex = 0;

  if (entries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = "No entries found";
    updateRequired = true;
    return;
  }

  state = BrowserState::BROWSING;
  updateRequired = true;
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  // Push current path to history before navigating
  navigationHistory.push_back(currentPath);
  currentPath = entry.href;

  state = BrowserState::LOADING;
  statusMessage = "Loading...";
  entries.clear();
  selectorIndex = 0;
  updateRequired = true;

  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    // At root, go home
    onGoHome();
  } else {
    // Go back to previous catalog
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();

    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    entries.clear();
    selectorIndex = 0;
    updateRequired = true;

    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = 0;
  downloadTotal = 0;
  updateRequired = true;

  // Build full download URL
  std::string downloadUrl = buildUrl(SETTINGS.opdsServerUrl, book.href);

  // Create sanitized filename
  std::string filename = "/" + sanitizeFilename(book.title) + ".epub";

  Serial.printf("[%lu] [OPDS] Downloading: %s -> %s\n", millis(), downloadUrl.c_str(), filename.c_str());

  const auto result =
      HttpDownloader::downloadToFile(downloadUrl, filename, [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        updateRequired = true;
      });

  if (result == HttpDownloader::OK) {
    Serial.printf("[%lu] [OPDS] Download complete: %s\n", millis(), filename.c_str());
    state = BrowserState::BROWSING;
    updateRequired = true;
  } else {
    state = BrowserState::ERROR;
    errorMessage = "Download failed";
    updateRequired = true;
  }
}

std::string OpdsBookBrowserActivity::sanitizeFilename(const std::string& title) const {
  std::string result;
  result.reserve(title.size());

  for (char c : title) {
    // Replace invalid filename characters with underscore
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      result += '_';
    } else if (c >= 32 && c < 127) {
      // Keep printable ASCII characters
      result += c;
    }
    // Skip non-printable characters
  }

  // Trim leading/trailing spaces and dots
  size_t start = result.find_first_not_of(" .");
  if (start == std::string::npos) {
    return "book";  // Fallback if title is all invalid characters
  }
  size_t end = result.find_last_not_of(" .");
  result = result.substr(start, end - start + 1);

  // Limit filename length (SD card FAT32 has 255 char limit, but let's be safe)
  if (result.length() > 100) {
    result.resize(100);
  }

  return result.empty() ? "book" : result;
}
