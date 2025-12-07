#include "BootLogoScreen.h"

#include <EpdRenderer.h>

#include "images/CrossLarge.h"

void BootLogoScreen::onEnter() {
  const auto pageWidth = renderer.getPageWidth();
  const auto pageHeight = renderer.getPageHeight();

  renderer.clearScreen();
  // Location for images is from top right in landscape orientation
  renderer.drawImage(CrossLarge, (pageHeight - 128) / 2, (pageWidth - 128) / 2, 128, 128);
  renderer.flushDisplay();
}
