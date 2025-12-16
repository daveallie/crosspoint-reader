#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Screen.h"

class CrossPointSettings;

// Enum to distinguish setting types
enum class SettingType { TOGGLE, ACTION };

// Structure to hold setting information
struct SettingInfo {
  const char* name;                         // Display name of the setting
  SettingType type;                         // Type of setting
  uint8_t CrossPointSettings::* valuePtr;   // Pointer to member in CrossPointSettings (for TOGGLE)
};

class SettingsScreen final : public Screen {
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
  explicit SettingsScreen(GfxRenderer& renderer, InputManager& inputManager, 
                         const std::function<void()>& onGoHome, 
                         const std::function<void()>& onGoWifi)
      : Screen(renderer, inputManager), onGoHome(onGoHome), onGoWifi(onGoWifi) {}
  void onEnter() override;
  void onExit() override;
  void handleInput() override;
};
