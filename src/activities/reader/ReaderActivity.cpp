#include "ReaderActivity.h"

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "CoverArtPickerActivity.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "FileSelectionActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/FullScreenMessageActivity.h"

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

bool ReaderActivity::isXtcFile(const std::string& path) {
  if (path.length() < 4) return false;
  std::string ext4 = path.substr(path.length() - 4);
  if (ext4 == ".xtc") return true;
  if (path.length() >= 5) {
    std::string ext5 = path.substr(path.length() - 5);
    if (ext5 == ".xtch") return true;
  }
  return false;
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load()) {
    return epub;
  }

  Serial.printf("[%lu] [   ] Failed to load epub\n", millis());
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  Serial.printf("[%lu] [   ] Failed to load XTC\n", millis());
  return nullptr;
}

void ReaderActivity::onSelectBookFile(const std::string& path) {
  currentBookPath = path;  // Track current book path
  exitActivity();
  enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Loading..."));

  if (isXtcFile(path)) {
    // Load XTC file
    auto xtc = loadXtc(path);
    if (xtc) {
      onGoToXtcReader(std::move(xtc));
    } else {
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Failed to load XTC", REGULAR,
                                                     EInkDisplay::HALF_REFRESH));
      delay(2000);
      onGoToFileSelection();
    }
  } else {
    // Load EPUB file
    auto epub = loadEpub(path);
    if (epub) {
      onGoToEpubReader(std::move(epub));
    } else {
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Failed to load epub", REGULAR,
                                                     EInkDisplay::HALF_REFRESH));
      delay(2000);
      onGoToFileSelection();
    }
  }
}

void ReaderActivity::onGoToFileSelection(const std::string& fromBookPath) {
  exitActivity();

  // Determine initial path based on default folder setting
  std::string initialPath;
  if (SETTINGS.defaultFolder == CrossPointSettings::FOLDER_LAST_USED) {
    // Use last browsed folder, or fall back to book's folder if coming from a book
    if (!fromBookPath.empty()) {
      initialPath = extractFolderPath(fromBookPath);
    } else if (!APP_STATE.lastBrowsedFolder.empty()) {
      initialPath = APP_STATE.lastBrowsedFolder;
    } else {
      initialPath = "/";
    }
  } else {
    // Use configured default folder (Root or Books)
    initialPath = SETTINGS.getDefaultFolderPath();
  }

  // Check if cover art picker is enabled
  if (SETTINGS.useCoverArtPicker) {
    enterNewActivity(new CoverArtPickerActivity(
        renderer, mappedInput, [this](const std::string& path) { onSelectBookFile(path); }, onGoBack, initialPath));
  } else {
    enterNewActivity(new FileSelectionActivity(
        renderer, mappedInput, [this](const std::string& path) { onSelectBookFile(path); }, onGoBack, initialPath));
  }
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  exitActivity();
  enterNewActivity(new EpubReaderActivity(
      renderer, mappedInput, std::move(epub), [this, epubPath] { onGoToFileSelection(epubPath); },
      [this] { onGoBack(); }));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { onGoToFileSelection(xtcPath); },
      [this] { onGoBack(); }));
}

void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (initialBookPath.empty()) {
    onGoToFileSelection();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}
