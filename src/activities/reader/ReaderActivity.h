#pragma once
#include <memory>

#include "../ActivityWithSubactivity.h"

class Epub;

class ReaderActivity final : public ActivityWithSubactivity {
  std::string initialEpubPath;
  const std::function<void()> onGoBack;
  static std::unique_ptr<Epub> loadEpub(const std::string& path);

  void onSelectEpubFile(const std::string& path);
  void onGoToFileSelection();
  void onGoToEpubReader(std::unique_ptr<Epub> epub);

 public:
  explicit ReaderActivity(GfxRenderer& renderer, InputManager& inputManager, std::string initialEpubPath,
                          const std::function<void()>& onGoBack)
      : ActivityWithSubactivity(renderer, inputManager),
        initialEpubPath(std::move(initialEpubPath)),
        onGoBack(onGoBack) {}
  void onEnter() override;
};
