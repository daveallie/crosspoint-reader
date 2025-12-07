#include "SleepScreen.h"

#include <EpdRenderer.h>

#include "images/SleepScreenImg.h"

void SleepScreen::onEnter() {
  renderer.clearScreen();
  renderer.flushDisplay();
  renderer.drawImageNoMargin(SleepScreenImg, 0, 0, 800, 480);
  renderer.flushDisplay();
}
