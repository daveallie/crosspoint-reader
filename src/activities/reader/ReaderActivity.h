#pragma once
#include <memory>

#include "../ActivityWithSubactivity.h"

class Epub;
class Xtc;

class ReaderActivity final : public ActivityWithSubactivity {
  std::string initialBookPath;
  std::string currentBookPath;  // Track current book path for navigation
  const std::function<void()> onGoBack;
  static std::unique_ptr<Epub> loadEpub(const std::string& path);
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
  static bool isXtcFile(const std::string& path);

  static std::string extractFolderPath(const std::string& filePath);
  void onSelectBookFile(const std::string& path);
  void onGoToFileSelection(const std::string& fromBookPath = "");
  void onGoToEpubReader(std::unique_ptr<Epub> epub);
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);

 public:
  explicit ReaderActivity(GfxRenderer& renderer, InputManager& inputManager, std::string initialBookPath,
                          const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("Reader", renderer, inputManager),
        initialBookPath(std::move(initialBookPath)),
        onGoBack(onGoBack) {}
  void onEnter() override;
};
