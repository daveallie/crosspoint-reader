#pragma once

#include <HardwareSerial.h>

#include <string>
#include <utility>

#include "../FrontButtonMapper.h"

class InputManager;
class GfxRenderer;

class Activity {
 protected:
  std::string name;
  GfxRenderer& renderer;
  InputManager& inputManager;
  mutable FrontButtonMapper frontButtonMapper;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, InputManager& inputManager)
      : name(std::move(name)), renderer(renderer), inputManager(inputManager), frontButtonMapper(inputManager) {}
  virtual ~Activity() = default;
  virtual void onEnter() { Serial.printf("[%lu] [ACT] Entering activity: %s\n", millis(), name.c_str()); }
  virtual void onExit() { Serial.printf("[%lu] [ACT] Exiting activity: %s\n", millis(), name.c_str()); }
  virtual void loop() {}
  virtual bool skipLoopDelay() { return false; }
};
