#include "CrossPointState.h"

#include <HardwareSerial.h>
#include <SD.h>
#include <Serialization.h>

#include <fstream>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 1;
constexpr char STATE_FILE[] = "/sd/.crosspoint/state.bin";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  std::ofstream outputFile(STATE_FILE);
  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, openEpubPath);
  outputFile.close();
  return true;
}

bool CrossPointState::loadFromFile() {
  std::ifstream inputFile(STATE_FILE);

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != STATE_FILE_VERSION) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  serialization::readString(inputFile, openEpubPath);

  inputFile.close();
  return true;
}
