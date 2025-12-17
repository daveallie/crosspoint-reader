#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class CrossPointSettings;

enum class SettingType { TOGGLE, ACTION };

// Structure to hold setting information
struct SettingInfo {
  const char* name;                        // Display name of the setting
  SettingType type;                        // Type of setting
  uint8_t CrossPointSettings::* valuePtr;  // Pointer to member in CrossPointSettings (for TOGGLE)
};

class SettingsActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedSettingIndex = 0;  // Currently selected setting
  const std::function<void()> onGoHome;
  const std::function<void()> onGoWifi;

  // Static settings list
  static constexpr int settingsCount = 3;  // Number of settings
  static const SettingInfo settingsList[settingsCount];

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void toggleCurrentSetting();
  void activateCurrentSetting();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, InputManager& inputManager, const std::function<void()>& onGoHome,
                            const std::function<void()>& onGoWifi)
      : Activity(renderer, inputManager), onGoHome(onGoHome), onGoWifi(onGoWifi) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
