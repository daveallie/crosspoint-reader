#include <Arduino.h>
#include <EpdRenderer.h>
#include <Epub.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <SPI.h>

#include "Battery.h"
#include "CrossPointState.h"
#include "Input.h"
#include "screens/BootLogoScreen.h"
#include "screens/EpubReaderScreen.h"
#include "screens/FileSelectionScreen.h"
#include "screens/FullScreenMessageScreen.h"
#include "screens/SleepScreen.h"

#define SPI_FQ 40000000
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define UART0_RXD 20  // Used for USB connection detection

#define SD_SPI_CS 12
#define SD_SPI_MISO 7

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC,
                                                                                                 EPD_RST, EPD_BUSY));
auto renderer = new EpdRenderer(&display);
Screen* currentScreen;
CrossPointState* appState;

// Power button timing
// Time required to confirm boot from sleep
constexpr unsigned long POWER_BUTTON_WAKEUP_MS = 1000;
// Time required to enter sleep mode
constexpr unsigned long POWER_BUTTON_SLEEP_MS = 1000;

Epub* loadEpub(const std::string& path) {
  if (!SD.exists(path.c_str())) {
    Serial.println("File does not exist");
    return nullptr;
  }

  const auto epub = new Epub(path, "/.crosspoint");
  if (epub->load()) {
    return epub;
  }

  Serial.println("Failed to load epub");
  delete epub;
  return nullptr;
}

void exitScreen() {
  if (currentScreen) {
    currentScreen->onExit();
    delete currentScreen;
  }
}

void enterNewScreen(Screen* screen) {
  currentScreen = screen;
  currentScreen->onEnter();
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress() {
  // Give the user up to 1000ms to start holding the power button, and must hold for POWER_BUTTON_WAKEUP_MS
  const auto start = millis();
  auto input = getInput(POWER_BUTTON_WAKEUP_MS);
  while (input.button != POWER && millis() - start < 1000) {
    delay(50);
    input = getInput(POWER_BUTTON_WAKEUP_MS);
  }

  if (input.button != POWER || input.pressTime < POWER_BUTTON_WAKEUP_MS) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_GPIO3, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
}

void waitForNoButton() {
  while (getInput().button != NONE) {
    delay(50);
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  exitScreen();
  enterNewScreen(new SleepScreen(renderer));

  Serial.println("Power button released after a long press. Entering deep sleep.");
  delay(1000);  // Allow Serial buffer to empty and display to update

  // Enable Wakeup on LOW (button press)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_GPIO3, ESP_GPIO_WAKEUP_GPIO_LOW);

  display.hibernate();

  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void onGoHome();
void onSelectEpubFile(const std::string& path) {
  exitScreen();
  enterNewScreen(new FullScreenMessageScreen(renderer, "Loading..."));

  Epub* epub = loadEpub(path);
  if (epub) {
    appState->openEpubPath = path;
    appState->saveToFile();
    exitScreen();
    enterNewScreen(new EpubReaderScreen(renderer, epub, onGoHome));
  } else {
    exitScreen();
    enterNewScreen(new FullScreenMessageScreen(renderer, "Failed to load epub", REGULAR, false, false));
    delay(2000);
    onGoHome();
  }
}

void onGoHome() {
  exitScreen();
  enterNewScreen(new FileSelectionScreen(renderer, onSelectEpubFile));
}

void setup() {
  setupInputPinModes();
  verifyWakeupLongPress();

  // Begin serial only if USB connected
  pinMode(UART0_RXD, INPUT);
  if (digitalRead(UART0_RXD) == HIGH) {
    Serial.begin(115200);
  }

  // Initialize pins
  pinMode(BAT_GPIO0, INPUT);

  // Initialize SPI with custom pins
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);

  // Initialize display
  const SPISettings spi_settings(SPI_FQ, MSBFIRST, SPI_MODE0);
  display.init(115200, true, 2, false, SPI, spi_settings);
  display.setRotation(3);  // 270 degrees
  display.setTextColor(GxEPD_BLACK);
  Serial.println("Display initialized");

  exitScreen();
  enterNewScreen(new BootLogoScreen(renderer));

  // SD Card Initialization
  SD.begin(SD_SPI_CS, SPI, SPI_FQ);

  appState = CrossPointState::loadFromFile();
  if (!appState->openEpubPath.empty()) {
    Epub* epub = loadEpub(appState->openEpubPath);
    if (epub) {
      exitScreen();
      enterNewScreen(new EpubReaderScreen(renderer, epub, onGoHome));
      // Ensure we're not still holding the power button before leaving setup
      waitForNoButton();
      return;
    }
  }

  exitScreen();
  enterNewScreen(new FileSelectionScreen(renderer, onSelectEpubFile));

  // Ensure we're not still holding the power button before leaving setup
  waitForNoButton();
}

void loop() {
  delay(50);

  const Input input = getInput();

  if (input.button == NONE) {
    return;
  }

  if (input.button == POWER && input.pressTime > POWER_BUTTON_SLEEP_MS) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    delay(1000);
    return;
  }

  if (currentScreen) {
    currentScreen->handleInput(input);
  }
}
